
import os
import string

fp = open("csv", "r")

class ASU:
	def __init__(self, asu):
		self.asu = asu
		self.reads = {}
		self.writes = {}

	def io(self, rw, offset):
		if rw == 0:
			self.read(offset)
		else:
			self.write(offset)

	def read(self, offset):
		try:
			self.reads[offset] += 1
		except:
			self.reads[offset] = 1

	def write(self, offset):
		try:
			self.writes[offset] += 1
		except:
			self.writes[offset] = 1

	def tocsv(self):
		rcsv = open("asu%d_r.csv" % self.asu, "w")
		wcsv = open("asu%d_w.csv" % self.asu, "w")

		for r in self.reads:
			rcsv.write("%s,%d\n" % (r, self.reads[r]))

		for w in self.writes:
			wcsv.write("%s,%d\n" % (w, self.writes[w]))

asus = [ ASU(1), ASU(2), ASU(3)]

while True:
	line = fp.readline()
	if line:
		csv = line.split(',')
		rw = int(csv[1].strip())
		asu = int(csv[2].strip())
		len = int(csv[3].strip())
		offset = int(csv[4].strip())

		while len > 0:
			asus[asu-1].io(rw, offset)
			offset += 1
			len -= 1
	else:
		break

for asu in asus:
	asu.tocsv()

