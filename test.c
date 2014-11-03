#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "spc1.h"
#include "tm.h"

int
main(int argc, char **argv)
{
	struct spc1_io_s s;
	int i, c, ios, bsu;
	const int CONTEXTS = 1;
	unsigned int ptime = 0;
	unsigned int msecs = 0;
	struct timespec ts, te;
	FILE *fp;

	fp = fopen("csv", "w");
	ios = 50000000; // 50 mil ios

	bsu = 200;
	spc1_init("test",
		bsu, // bsu
		45*1024*1024 / 4, // 45G as 4k blocks
		45*1024*1024 / 4, // 45G as 4k blocks
		10*1024*1024 /4, // 10G as 4k blocks
		CONTEXTS, // contexts
		NULL, // version
		0);

	TM_NOW(ts);
	for (i=0; i<ios; i++){
		for (c=0; c<CONTEXTS; c++) {
			spc1_next_op(&s, c);
			/*
			printf("%d:%d:asu=%u:"
				"rw=%u:"
				"len=%u:"
				"stream=%u:"
				"bsu=%u:"
				"offset=%u:"
				"when=%u\n",
				i, c,
				s.asu,
				s.dir,
				s.len,
				s.stream,
				s.bsu,
				s.pos,
				s.when);
				*/
			//usleep((s.when-ptime)*100);
			//ptime=s.when;

			fprintf(fp, "%d," // io 1
				"%d," // read/write 2
				"%d," // asu 3
				"%d," // len 4
				"%d\n", // offset 5
				i,
				s.dir,
				s.asu,
				s.len,
				s.pos);
		}
	}
	fclose(fp);
	TM_NOW(te);
	msecs = TM_DURATION_N2MSEC(TM_DURATION_NSEC(te, ts));
	printf("ms = %d\n", msecs);
	/*
	printf("IOPS = %d\n", ios/(msecs/1000));
	printf("Exp IOPST = %d\n", (bsu*50));
	*/

	return 0;

}