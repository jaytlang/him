#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/queue.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <pwd.h>
#include <seccomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#define SERVER_PORT		6969
#define SERVER_USER		"him"
#define SERVER_CHROOT		"/var/empty"
#define SERVER_BACKLOG		128

#define HIM_STATE_RECV		0
#define HIM_STATE_SEND		1

#define LED_COLOR_RED		1
#define LED_COLOR_GREEN		2
#define LED_COLOR_BLUE		4

#define LED_COLOR_YELLOW	(LED_COLOR_RED | LED_COLOR_GREEN)
#define LED_COLOR_TURQUOISE	(LED_COLOR_GREEN | LED_COLOR_BLUE)
#define LED_COLOR_PURPLE	(LED_COLOR_BLUE | LED_COLOR_RED)

#define LED_COLOR_MAX		(LED_COLOR_RED | \
				 LED_COLOR_GREEN | \
				 LED_COLOR_BLUE)

struct him {
	struct event		ev;
	int			sockfd;
	int			sent;
	SLIST_ENTRY(him)	entries;
};

SLIST_HEAD(devlist, him) devlist = SLIST_HEAD_INITIALIZER(devlist);

static int		listenfd = -1;
static struct event	listenev;
static int		sending = 0;
static char		color = LED_COLOR_RED;

static void		him_new(int);
static void		him_recv(int, short, void *);
static void		him_send(int, short, void *);
static void		him_teardown(struct him *);
static void		him_change_state(int);
static int		him_done_sending(void);
static void		him_accept(int, short, void *);

static void		privdrop(void);

static void
him_new(int sockfd)
{
	struct him	*out;

	if ((out = malloc(sizeof(struct him))) == NULL)
		err(1, "him_new: malloc");

	out->sockfd = sockfd;
	out->sent = 0;

	if (!sending) event_set(&out->ev, sockfd, EV_READ, him_recv, out);
	else event_set(&out->ev, sockfd, EV_WRITE, him_send, out);

	if (event_add(&out->ev, NULL) < 0)
		err(1, "him_new: event_add");

	SLIST_INSERT_HEAD(&devlist, out, entries);
}

static void
him_recv(int fd, short event, void *arg)
{
	struct him	*h = (struct him *)arg;
	ssize_t		 bytesread;

	for (;;) {
		bytesread = read(fd, &color, sizeof(char));
		if (bytesread == -1) {
			if (errno == EWOULDBLOCK) {
				if (!sending && event_add(&h->ev, NULL) < 0)
					err(1, "him_recv: event_add");
				return;
			} else err(1, "him_recv: read");
		}

		if (bytesread == 0) {
			him_teardown(h);
			return;
		}

		warnx("received new color %d from fd %d", color, fd);
		him_change_state(HIM_STATE_SEND);
	}

	(void)event;
}

static void
him_send(int fd, short event, void *arg)
{
	struct him	*h = (struct him *)arg;
	ssize_t		 byteswritten;

	(void)event;

	byteswritten = write(fd, &color, sizeof(char));
	if (byteswritten == -1) {
		if (errno == EWOULDBLOCK) {
			if (event_add(&h->ev, NULL) < 0)
				err(1, "him_send: event_add");
			return;
		} else err(1, "him_send: write");
	}

	if (byteswritten == 0) him_teardown(h);

	warnx("sent new color to fd %d", fd);
	h->sent = 1;
	if (him_done_sending()) him_change_state(HIM_STATE_RECV);
}

static void
him_teardown(struct him *h)
{
	if (event_del(&h->ev) < 0) err(1, "him_teardown: event_del");
	SLIST_REMOVE(&devlist, h, him, entries);
	close(h->sockfd);

	warnx("tearing down connection (fd %d)", h->sockfd);
	free(h);

	if (him_done_sending()) him_change_state(HIM_STATE_RECV);
}

static void
him_change_state(int newstate)
{
	struct him	 *p;
	short		  listenfor;
	void		(*cb)(int, short, void *);

	if (newstate == sending) return;
	else sending = newstate;
	warnx("changed state to sending = %d", sending);

	listenfor = (sending) ? EV_WRITE : EV_READ;
	cb = (sending) ? him_send : him_recv;

	SLIST_FOREACH(p, &devlist, entries) {
		p->sent = 0;

		if (event_del(&p->ev) < 0)
			err(1, "him_change_state: event_del");

		event_set(&p->ev, p->sockfd, listenfor, cb, p);
		if (event_add(&p->ev, NULL) < 0)
			err(1, "him_change_state: event_add");
	}
}

static int
him_done_sending(void)
{
	struct him	*p;

	SLIST_FOREACH(p, &devlist, entries) if (p->sent == 0) return 0;
	warnx("done sending");
	return 1;
}

static void
him_accept(int fd, short event, void *arg)
{
	int	sockfd;

	(void)event;
	(void)arg;

	sockfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK|SOCK_CLOEXEC);
	if (sockfd == -1) {
		if (errno == EWOULDBLOCK) return;
		else err(1, "him_accept: accept");
	}

	warnx("accepting new connection -> fd %d", sockfd);
	him_change_state(HIM_STATE_SEND);
	him_new(sockfd);
}

#define SECCOMP_ALLOW(CTX, SYS) do {						\
	if (seccomp_rule_add(CTX, SCMP_ACT_ALLOW, SCMP_SYS(SYS), 0) < 0)	\
		err(1, "privdrop: seccomp_rule_add %s", #SYS);			\
} while (0)

static void
privdrop(void)
{
	struct passwd	*user;
	scmp_filter_ctx	 scctx;

	/* get user to drop to later, and
	 * set up underlying seccomp infrastructure
	 */
	if ((user = getpwnam(SERVER_USER)) == NULL)
		err(1, "privdrop: getpwnam");

	/* chroot */
	if (chroot(SERVER_CHROOT) < 0)
		err(1, "privdrop: chroot");

	/* drop user privilege */
	if (setresgid(user->pw_gid, user->pw_gid, user->pw_gid) < 0)
		err(1, "privdrop: setresgid");
	else if (setresuid(user->pw_uid, user->pw_uid, user->pw_uid) < 0)
		err(1, "privdrop: setresuid");

	/* seccomp */
	if ((scctx = seccomp_init(SCMP_ACT_TRAP)) == NULL)
		err(1, "privdrop: seccomp_init");

	SECCOMP_ALLOW(scctx, read);
	SECCOMP_ALLOW(scctx, write);
	SECCOMP_ALLOW(scctx, accept4);
	SECCOMP_ALLOW(scctx, close);
	SECCOMP_ALLOW(scctx, epoll_create);
	SECCOMP_ALLOW(scctx, epoll_ctl);
	SECCOMP_ALLOW(scctx, epoll_pwait);

	if (seccomp_load(scctx) < 0) err(1, "privdrop: seccomp_load");
}

int
main()
{
	struct sockaddr_in	sa;
	int			enable = 1;

	if (getuid() != 0)
		errx(1, "this program must be run as root");

	event_init();

	listenfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (listenfd < 0) err(1, "main: socket");

	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
	    &enable, sizeof(int)) < 0)
		err(1, "main: setsockopt SO_REUSEADDR");

	bzero(&sa, sizeof(struct sockaddr_in));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(SERVER_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(listenfd, (struct sockaddr *)&sa,
	    sizeof(struct sockaddr_in)) < 0)
		err(1, "main: bind");

	if (listen(listenfd, SERVER_BACKLOG) < 0)
		err(1, "main: listen");

	event_set(&listenev, listenfd, EV_READ|EV_PERSIST, him_accept, NULL);
	if (event_add(&listenev, NULL) < 0)
		err(1, "main: event_add");

	privdrop();
	event_dispatch();
	/* never reached */
	return 0;
}

