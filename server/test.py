import socket
import sys
import random

SERVER_HOST = "129.80.111.96"
SERVER_PORT = 6969

if len(sys.argv) != 2 or (sys.argv[1] != 'update' and sys.argv[1] != 'monitor'):
	print(f"usage: {sys.argv[0]} update | monitor")
	print(sys.argv)
	sys.exit(2)

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((SERVER_HOST, SERVER_PORT))

if sys.argv[1] == "update":
	# read current
	current = int.from_bytes(s.recv(1), 'big')
	print(f"current color is {current}")

	# update...
	newcolor = random.randint(1, 6)
	print(f"updating color to {newcolor}")
	s.sendall(newcolor.to_bytes(1, 'big'))

	# read new color
	current = int.from_bytes(s.recv(1), 'big')
	assert current == newcolor, "new color does not match"

	s.close()
	sys.exit(0)

while True:
	# read whenever we can
	rawdata = s.recv(1)
	if len(rawdata) == 0: break
	print(f"current color is {int.from_bytes(rawdata, 'big')}")
