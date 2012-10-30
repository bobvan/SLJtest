/**
 * \file   sjtest.c
 * \brief  System Jitter Test
 * \author Robert A. Van Valzah - Informatica Corporation
 *
 * System Jitter Test - Measure and visualize system jitter
 *
 * See accompanying XXX PDF or HTML renderings of doc or doc input at end of this file
 *  
 * Termnology used in the source:
 *  Ticks:
 *	One tick is the time taken for the Time Stamp Counter (TSC) on
 *	x86 processors to advance one count.
 *	The inverse of the CPU clock frequency is the time taken for
 *	one TSC tick in seconds.
 *
 *  Timestamp:
 *	One reading of TSC.
 *
 *  Block of timestamps:
 *	Timestamps taken in sequence to keep control flow uninterupted by branches.
 *	Statistics and logging are done after the block.
 *	This maximizes cache hit rates and keeps the CPU executing
 *	at full speed.
 *
 *  Delta:
 *	The difference between the TSC on two sequential readings.
 *
 *  Bin:
 *	One entry in the histogram table
 *
 *  Knee:
 *	The midpoint in the number of histogram bins: 1/2 the bins below
 *	the knee, 1/2 the bins above the knee.
 * 	Bins step linearaly below the knee and exponentially above it.
 *
 * ToDo
 *  Taskset
 *  CPU affiinity
 *  Maybe option to run many threads at once?
 *  Windows port
 *  Better comments on data structures and algoriths
 *  Break up main() so it's not so long
 *
 *
 * (c) Copyright 2011, 2012 Informatica Corp.
 * Bob Van Valzah, October 2011, February 2012, October 2012
 *
 * Acknowledgement
 *  Inspired by David Riddoch of Solarflare
 *
 * License
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction.  The author will
 * be disappointed if you use this software for making war, mulching
 * babies, or improving systems running competing messaging software.
 *
 * Discalimers
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL
 *  WARRANTIES EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
 *  IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS
 *  FOR A PARTICULAR PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE
 *  OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.  INFORMATICA
 *  SHALL NOT, UNDER ANY CIRCUMSTANCES, BE LIABLE TO LICENSEE FOR LOST
 *  PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR INDIRECT DAMAGES
 *  ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE TRANSACTIONS
 *  CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
 *  THE LIKELIHOOD OF SUCH DAMAGES.
 *
 */

/*! Has side effect of declaring asprintf() */
#define	_GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "replgetopt.h"

#ifdef	CPU_AFFINITY
#define	__USE_GNU
#include <sched.h>
#include "bitmask.h"
#endif	/* CPU_AFFINITY */

#if defined(_WIN32)
#include <sys/timeb.h>
#define WIN32_HIGHRES_TIME
extern int optind;
extern char *optarg;
int getopt(int, char *const *, const char *);
/*! Waste time for x      seconds */
#   define SLEEP_SEC(x) Sleep((x)*1000)
/*! Waste time for x microseconds */
#   define SLEEP_MSEC(x) Sleep(x)

#else
/*! Waste time for x      seconds */
#   define SLEEP_SEC(x) sleep(x)
/*! Waste time for x microseconds */
#   define SLEEP_MSEC(x) \
		do{ \
			if ((x) >= 1000){ \
				sleep((x) / 1000); \
				usleep((x) % 1000 * 1000); \
			} \
			else{ \
				usleep((x)*1000); \
			} \
		}while (0)
#endif /* _WIN32 */


/*! DEFault number of histogram BINS */
#define	DEF_BINS		20
/*! DEFault CPU affinity */
#define	DEF_CPU			NULL
/*! DEFault OUTput FILEname */
#define	DEF_OUTFILE		NULL
/*! DEFault KNEE value (ticks) */
#define	DEF_KNEE		50
/*! DEFault MINimum delta (ticks) */
#define	DEF_MIN			30
/*! DEFault OUTlier BUFfer */
#define	DEF_OUTBUF		10000
/*! DEFault PAUSE time before each block in microseconds */
#define	DEF_PAUSE		0
/*! DEFault RUN TIME in seconds */
#define	DEF_RUNTIME		1
/*! DEFault maximum output LINE WIDth */
#define	DEF_LINEWID		79

/*! \brief Read value of TSC into a uint64_t
 *  \param x A uint64_t to receive the TSC value
 */
#define rdtsc(x) \
	do { \
		uint32_t hi, lo; \
		asm volatile ("rdtsc" : "=a" (lo), "=d" (hi)); \
		x = (uint64_t)hi << 32 | lo; \
	} while (0)

/*! Size of array a in elements */
#define	ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

/*! Command line argument values */
typedef struct args_stct {
/*! Number of bins in histogram */
	int bins;
#ifdef	CPU_AFFINITY
/*! CPU affinity */
	char *cpu;
#endif	/* CPU_AFFINITY */
/*! OUTput FILEname */
	char *outfile;
/*! Knee of histogram curve (ticks) */
	uint64_t knee;
/*! Minimum expected value (ticks) */
	uint64_t min;
/*! OUTlier BUFfer size (outliers) */
	int outbuf;
/*! Pause before each block (milliseconds) */
	int pause;
/*! Runtime (seconds) */
	int runtime;
/*! Output line width (characters) */
	int linewid;
} args_t;

/*! Type for histogram table */
typedef struct bin_stct {
/*! Upper Bound (inclusive) on histogram bin (ticks) */
	uint64_t ub;
/*! Number of samples <= ub */
	int count;
} bin_t;

/*! Type for outlier buffer entry */
typedef struct outlier_stct {
/*! TSC when the outlier happended */
	uint64_t when;
/*! Size of outlier (ticks) */
	uint64_t delta;
} outlier_t;

/*! Command line argument values */
args_t args = {
	DEF_BINS,
#ifdef	CPU_AFFINITY
	DEF_CPU,
#endif	/* CPU_AFFINITY */
	DEF_OUTFILE,
	DEF_KNEE,
	DEF_MIN,
	DEF_OUTBUF,
	DEF_PAUSE,
	DEF_RUNTIME,
	DEF_LINEWID,
};

/*! Command line options for getopt() */
const struct option OptTable[] = {
	{"bins",    required_argument, NULL, 'b'},
#ifdef	CPU_AFFINITY
	{"cpu",     required_argument, NULL, 'c'},
#endif	/* CPU_AFFINITY */
	{"outfile", required_argument, NULL, 'f'},
	{"help",          no_argument, NULL, 'h'},
	{"knee",    required_argument, NULL, 'k'},
	{"min",     required_argument, NULL, 'm'},
	{"outbuf",  required_argument, NULL, 'o'},
	{"pause",   required_argument, NULL, 'p'},
	{"runtime", required_argument, NULL, 'r'},
	{"width",   required_argument, NULL, 'w'},
};

#ifdef	CPU_AFFINITY
const char *OptString = "b:c:f:hk:m:o:p:r:w:";
const char *usage = "[-b bins] [-c cpu] [-f file] [-h] [-k knee] [-m min] [-o outbuf] [-p pause] [-r runtime] [-w width]";

#else	/* CPU_AFFINITY */
const char *OptString = "b:f:hk:m:o:p:r:w:";
const char *usage = "[-b bins] [-f file] [-h] [-k knee] [-m min] [-o outbuf] [-p pause] [-r runtime] [-w width]";
#endif	/* CPU_AFFINITY */

/*! Note that Makefile parses the following line to extract version number */
const char *version = "SJ Test 0.8b";

/*! Histogram table of timestamp deltas */
bin_t *histo;

/*! Ring BUFfer of recent OUTliers */
outlier_t *outbuf = NULL;
/*! FILE where we write OUTliers */
FILE *outfile = NULL;

#ifdef	CPU_AFFINITY
/*!
 * \brief Set CPU affinity
 * \param affinity CPUs for affinity expressed as a character string
 */
static void
set_affinity(const char* affinity) {
        struct bitmask *bm;

        fprintf(stderr, "Requesting CPU affinity on core %s\n", affinity);
	if ((bm=bitmask_alloc(__CPU_SETSIZE)) == NULL) {
		fprintf(stderr, "bitmask_alloc failed\n");
		exit(1);
	}
	if (bitmask_parselist(affinity, bm) != 0) {
		fprintf(stderr, "bitmask_parselist failed\n");
		exit(1);
	}
        if (sched_setaffinity(0, bitmask_nbytes(bm),
	    (cpu_set_t *)bitmask_mask(bm)) < 0) {
                fprintf(stderr, "%s: failed to set affinity to %s\n", affinity);
	}
}
#endif	/* CPU_AFFINITY */

#ifndef HAVE_ASPRINTF
/*! \brief sprintf() to a malloc()'d string
 *  \param ret Pointer to char * where output string will be returned
 *  \param format printf()-style format string
 *  \param ... Optional arguments a la printf()
 * \return 0
 */
/*! Our own hack version of asprintf() for platforms that don't have it.
 * I'm looking at you Solaris.
 */
int
#define	RET_MAX_LEN 10	/* Maximum length of return string */
asprintf(char **ret, const char *format, ...) {
	va_list ap;

	va_start(ap, format);
	if ((*ret=malloc(RET_MAX_LEN)) == NULL) {
		fprintf(stderr, "asprintf: malloc() failed\n");
		return(-1);
	}
	vsnprintf(*ret, RET_MAX_LEN, format, ap);
	va_end(ap);
	return(0);
}
#endif	/* HAVE_ASPRINTF */

/*!
 * \brief Convert a number of TSC ticks to a time string easily human readable.
 * \param ticks Ticks to be converted.
 * \param tpns  Ticks per nanosecond as measured for this system.
 *
 * Value is scaled to nanoseconds, microseconds, milliseconds, or seconds.
 * Returns a malloc()'d string, so caller must free() or leak.
 */
char *
t2ts(uint64_t ticks, double tpns) {
	double ns = ticks/tpns;
	char *s;

	     if (ns < 1E3 ) asprintf(&s, "%4.3gns", ns);
	else if (ns < 1E6 ) asprintf(&s, "%4.3gus", ns/1E3);
	else if (ns < 1E9 ) asprintf(&s, "%4.3gms", ns/1E6);
	else if (ns < 1E12) asprintf(&s, "%4.3gs",  ns/1E9);
	else                asprintf(&s, "Infini");

	return (s);
}

/*
 * \brief  Parse command line arguments.
 * \param  argc Count of arguments as passed to main().
 * \param  argv Argument vector as passed to main().
 * \return 0 if no parse errors, 1 otherwise.
 */
int
args_parse(int argc, char *argv[]) {
	int c;

	while ((c=getopt_long(argc, argv, OptString, OptTable, NULL)) != EOF) {
        	switch (c) {

		case 'b':
			args.bins    = atoi(optarg);
			break;

#ifdef	CPU_AFFINITY
		case 'c':
			args.cpu     = strdup(optarg);
			break;
#endif	/* CPU_AFFINITY */

		case 'f':
			args.outfile = strdup(optarg);
			break;

		case 'k':
			args.knee    = atoi(optarg);
			break;

		case 'm':
			args.min     = atoi(optarg);
			break;

		case 'p':
			args.pause   = atoi(optarg);
			break;

		case 'r':
			args.runtime = atoi(optarg);
			break;

		case 'w':
			args.linewid = atoi(optarg);
			break;

		case 'h':
		default:
			return (1);
		}
	}
	return (0);
}

/*! \brief Allocate memory and open file for outliers logging
 */
void
outliers_setup() {
	if ((outbuf=(outlier_t *)calloc(args.outbuf, sizeof(outlier_t))) == NULL) {
		fprintf(stderr, "Couldn't allocate memory for outlier buffer\n");
		exit(1);
	}
	if ((outfile=fopen(args.outfile, "w")) == NULL) {
		fprintf(stderr, "Unable to create outliers file %s\n",
		    args.outfile);
		perror(args.outfile);
		exit(1);
	}
}

void
histo_setup() {
	bin_t *bp;

	/* Allocate memory for histogram */
	if ((histo=(bin_t *)malloc(sizeof(bin_t)*args.bins)) == NULL) {
		fprintf(stderr, "Couldn't allocate memory for histogram bins\n");
		exit(1);
	}

	/*
	 * Fill first half of histogram table with values up to knee.
	 * Evenly divide upper bound values through knee among bins.
	 */
	for (bp=histo; bp<histo+(args.bins/2); bp++) {
		bp->ub = args.min+(args.knee-args.min)*(bp-histo+1)/(args.bins/2);
		bp->count = 0;
	}
	/* Fill second half of histogram table with values above knee */
	/* Advance each bin upper bound by 1/2 an order of magnitude */
	uint64_t mult = args.knee;
	for (        ; bp<histo+args.bins; bp++) {
		bp->ub = mult*2;
		bp->count = 0;
		bp++;

		mult *= 10;
		bp->ub = mult;
		bp->count = 0;
	}
	histo[args.bins-1].ub = UINT64_MAX;	/* Sentinel */
}

/*!
 * \brief Measure and visualize system jitter.
 * \param argc Count of arguments
 * \param argv Argument vector
 */
int
main(int argc, char *argv[]) {
	int errflag;
	uint64_t start_us, stop_us, now_us; /* Start, stop, and time now in microseconds */
	uint64_t start_tsc, stop_tsc;	/* Start and stop in TSC ticks */
	bin_t *bp;
	uint64_t deltas[10], *dp;
	uint64_t min, max;		/* Min and max deltas (ticks) */
	struct timeval now_gtod;	/* Time now as timeval */
	const char *pre_graph_hdr = "Time    Ticks    Count        Percent    Cumulative  ";
	const char *graph_str = "*******************************************************************";
	outlier_t *obp;		/* Pointer to next open entry in outlier buffer */
	int didwrap;		/* True when outlier buffer wrapped around */

	errflag = args_parse(argc, argv);

	/* Argument validity checks */
	if (args.knee <= args.min) {
		fprintf(stderr, "Min (%" PRIu64
		    ") must be < knee (%" PRIu64 ")\n",
		    args.min, args.knee);
		errflag++;
	}
	if (args.knee-args.min < args.bins/2) {
		fprintf(stderr, "Too few (%" PRIu64
		    ") discrete values between min (%" PRIu64
		    ") and knee (%" PRIu64 ") for linear histogram bins (%d)\n",
		    args.knee-args.min, args.min, args.knee, args.bins/2);
		errflag++;
	}
	if (args.linewid < strlen(pre_graph_hdr)+1) {
		fprintf(stderr, "Minimum line width is %zd\n", strlen(pre_graph_hdr)+1);
		errflag++;
	}
	if (args.linewid > strlen(pre_graph_hdr)+strlen(graph_str)) {
		fprintf(stderr, "Maximum line width is %zd\n",
		strlen(pre_graph_hdr)+strlen(graph_str));
		errflag++;
	}

	if (errflag) {
		fprintf(stderr, "%s\n%s %s\n", version, argv[0], usage);
		exit(1);
	}

#ifdef	CPU_AFFINITY
	if (args.cpu != NULL)
		set_affinity(args.cpu);
#endif	/* CPU_AFFINITY */

	if (args.outfile!=NULL && args.outbuf!=0) {
		outliers_setup();	/* Set up memory and output file for outliers */
	}

	histo_setup();		/* Set up histogram memory and data structures */

	/*
	 * XXX Note that it might get much eaiser to modularize this if in the
	 * future we set up for multiple threads.
	 */

	uint64_t timing_ticks = 0;	/* TSC ticks actually spent in timing measurements */
	uint64_t delta_count = 0;	/* Count of deltas taken */
	uint64_t delta_sum = 0;		/* Sum of TSC deltas measured */
	min   = UINT64_MAX;
	max   = 0;

	obp = outbuf;
	didwrap = 0;

	/*
	 * Variables for computing average and standard deviation.
	 * See http://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#On-line_algorithm
	 */
	double avg = 0.0;	/* Average delta (ticks) */
	double last_avg;	/* Last average (needed for deviation */
	double svn = 0.0;	/* Standard Variance Numerator */

	rdtsc(start_tsc);
	gettimeofday(&now_gtod, NULL);
	start_us = now_gtod.tv_sec * 1000000 + now_gtod.tv_usec;
	stop_us = start_us + 1000000*args.runtime;

	do {
		dp = deltas;

		if (args.pause)
			SLEEP_MSEC(args.pause);

		register uint64_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10;

		/*
		 * Take a block of 11 timestamps and compute their differences into
		 * a 10-element array.
		 */
		/* Do an "unrolled loop" so there's no branching or other work */
		rdtsc(t0);
		rdtsc(t1);
		rdtsc(t2);
		rdtsc(t3);
		rdtsc(t4);
		rdtsc(t5);
		rdtsc(t6);
		rdtsc(t7);
		rdtsc(t8);
		rdtsc(t9);
		rdtsc(t10);
		*dp++ = t1 -t0;
		*dp++ = t2 -t1;
		*dp++ = t3 -t2;
		*dp++ = t4 -t3;
		*dp++ = t5 -t4;
		*dp++ = t6 -t5;
		*dp++ = t7 -t6;
		*dp++ = t8 -t7;
		*dp++ = t9 -t8;
		*dp++ = t10-t9;

		timing_ticks += t10-t0;

		/*
		 * Now that we're out of the timing loop, we can take all the
		 * CPU we need for analysis.
		 */
		for (dp=deltas; dp<deltas+ARRAY_SIZE(deltas); dp++) {
			if (*dp < min)
				min   = *dp;
			if (*dp > max)
				max   = *dp;

			/* Find bin to count this delta */
			/* Note: no end test is needed because of infinite sentinel */
			for (bp=histo; *dp>bp->ub; bp++) {}
			bp->count++;

			delta_count++;
			delta_sum += *dp;

			last_avg = avg;
			avg += ((double)*dp-avg)/delta_count;
			svn += ((double)*dp-avg)*((double)*dp-last_avg);

			/* If an outlier should be recorded */
			if (outbuf!=NULL && *dp>args.knee) {
				/* Assume it happened in middle of block */
				obp->when = t5;
				obp->delta = *dp;
				obp++;
				/* Wrap around if needed */
				if (obp-outbuf >= args.outbuf) {
					obp = outbuf;
					didwrap = 1;
				}
			}
		}
		rdtsc(stop_tsc);
		gettimeofday(&now_gtod, NULL);
		now_us = now_gtod.tv_sec * 1000000 + now_gtod.tv_usec;

	} while (now_us < stop_us);

	/* Compute Ticks Per NanoSecond for duration of test */
	double tpns = (stop_tsc-start_tsc)/1000.0/(stop_us-start_us);

	/* Print histogram headers */
	printf("%sGraph ln(Count-e)\n", pre_graph_hdr);

	/* Find maximum count in any histogram bin for scaling graph */
	uint64_t max_count = 0;
	for (bp=histo; bp<histo+args.bins; bp++) {
		if (bp->count > max_count)
			max_count = bp->count;
	}
	double graph_scale = (double)(args.linewid-strlen(pre_graph_hdr))/log(((double)max_count)-M_E);

	/* Print histogram */
	uint64_t c_count = 0;	/* Cumulative count as we step through bins */
	uint64_t mid_count = 0;	/* Cumulative count at histogram midpoint */
	for (bp=histo; bp<histo+args.bins; bp++) {
		/*
		 * If at midpoint in histogram, record count_sum for
		 * knee tuning advice later.
		 */
		if (bp == histo+(args.bins/2))
			mid_count = c_count;
		c_count += bp->count;

		/* Format bin upper bound nicely into ub_str */
		char *ub_str, ubbuf[99];
		if (bp->ub == UINT64_MAX)
			ub_str = "Infinite";
		else {
			sprintf(ubbuf, "%-8" PRIu64, bp->ub);
			ub_str = ubbuf;
		}

		/* Compute a logrithmic function on bin count that looks good */
		int countlog = graph_scale*log(((double)bp->count)-M_E);
		if (countlog < 0)
			countlog = 0;
		/*
		 * Any nonzero count deserves a star, even though floating
		 * point sometimes rounds down to zero stars.
		 */
		if (countlog==0 && bp->count!=0)
			countlog = 1;

		/* Print a row for each bin */
		printf("%s  %s %-12d %7.4f%%  %8.4f%%    %*.*s\n",
		    t2ts(bp->ub, tpns), ub_str, bp->count,
		    100.0*bp->count/delta_count, 100.0*c_count/delta_count,
		    countlog, countlog, graph_str);

		/* Separate lower 1/2 of histogram from upper 1/2 */
		if (bp-histo+1 == args.bins/2)
			printf("\n");
	}

	/*
	 * Population variance is svn sum computed above over size of population.
	 * Population standard deviation is square root of population variance.
	 */
	double std_dev = sqrt(svn/delta_count);

	/* Print some useful statistics */
	printf("\nTiming was measured for %s, %5.2f%% of runtime\n",
	    t2ts(timing_ticks, tpns), 100.0*timing_ticks/(stop_tsc-start_tsc));
	printf("CPU speed measured  : %7.2f MHz over %" PRIu64 " iterations\n",
	    (double)(stop_tsc-start_tsc)/(stop_us-start_us), delta_count);
	printf("Min / Average / Std Dev / Max :   %" PRIu64 "   /   %" PRIu64 "   /  %3.0f   / %" PRIu64 " ticks\n",
	    min, delta_sum/delta_count, std_dev, max);
	printf("Min / Average / Std Dev / Max : %s / %s / %s / %s\n",
	    t2ts(min, tpns), t2ts(delta_sum/delta_count, tpns),
	    t2ts((uint64_t)std_dev, tpns), t2ts(max, tpns));

	/* Analyze histogram to give advice on setting better min and knee */
	if (min<args.min || args.min<0.80*min) {
		printf("Recommend min setting %3.0f ticks\n", 0.80*min);
	}
	if (outbuf==NULL && 100.0*mid_count/delta_count<90.0) {
		printf("Recommend increasing knee setting from %" PRIu64 " ticks\n",
		    args.knee);
	}
	if (outbuf==NULL && 100.0*mid_count/delta_count>99.0) {
		printf("Recommend decreasing knee setting from %" PRIu64 " ticks\n",
		    args.knee);
	}

	/* Dump log of outliers to outfile */
	/* XXX Why isn't this a compound && test like the above call to outliers_setup()? */
	if (outbuf != NULL) {
		if (didwrap) {
			printf("Recommend increasing knee setting from %" PRIu64 " ticks\n",
			    args.knee);
		} else if (obp-outbuf < args.outbuf/4) {
			printf("Recommend decreasing knee setting from %" PRIu64 " ticks\n",
			    args.knee);
		}
		for (obp=outbuf; obp<outbuf+args.outbuf; obp++) {
			if (obp->when == 0)
				continue;
			fprintf(outfile, "%f, %f\n",
			    (obp->when-start_tsc)/tpns/1000000.0,
			    obp->delta/tpns/1000.0);
		}
		fclose(outfile);
	}
	return (0);
}
/**
\mainpage SJ Test - System Jitter Test

Links to cool stuff
\li item one
\li item two
\li Browse code by <a href="globals.html"><b>functions, variables, defines, enums, and typedefs</b></a>.

\page sjtest System Jitter Test Overview
 
\section impatient For the Impatient

\li Just run it: make; ./sjtest XXX fix for new build procedure
\li Add possibility of running from binary XXX
\li The output should be pretty self explanatory.
\li Put your right ear on your shoulder to see the histogram.
\li See section Output Explanation below for details.
\li See section Command Line Options for fine tuning and features.
\li See ObservationsOflowLevelJitter.pdf for analysis


\section introduction Introduction

Ultra Messaging customers use our software to build complicated
systems that often process millions of messages per second.  Common
goals are low latency and high throughput, but many customers also
value freedom from latency jitter.  A system with no latency jitter would
show exactly the same latency for every message passing through it.
Consistency is the hallmark of low-jitter systems.

It's hard to be consistent in doing repetitive work when you're being
interrupted all the time.  For humans, consistency in performing a
repeated task requires freedom from interruptions.

Roughly the same idea applies to computer systems.
SJ Test measures the ability of a system to provide a CPU core that is
able to consistently execute a repetitive task.

Applications doing repetitive tasks may introduce their own jitter
by slightly varying the task.
Even when code paths appear to be free from variance, jitter can be
caused by library calls like <tt>malloc()</tt> and <tt>free()</tt>
that necessarily contain their own conditional code paths.

We define system jitter as the jitter measured by an application
that introduces no jitter of its own.
System jitter will be present in any application run by the system.
It represents the lower bound for jitter that might be expected of
any application running on the system.

SJ Test measures system jitter and provides a simple but effective
visualization of it for analysis.

XXX List some interesting things that can be measured, like the jitter cost
of a thread that sleeps versus the jitter on a hot thread.


\section methodology Time Measurement Methodology

The CPU's Time Stamp Counter (TSC) is useful for measuring how
long tasks take, especially when very short times are involved.
It's usually counting at a rate of 3 or 4 billion counts per second.
That gives it a resolution of 333 to 250 picoseconds.

Elapsed ticks can be measured by reading the TSC before and after
doing a task, then subtracting the timestamps.
This gives the number of ticks taken by the task, plus the time
taken to read the TSC once.

 rdtsc(start);
 // Do some task
 rdtsc(stop);
 elapsed_ticks = stop-start;

Elapsed time in seconds can be computed by dividing elapsed ticks by
CPU frequency in Hertz.

If this process is repeated many times, you'll undoubtedly see
some variation in the number of ticks taken to do the task, even
though the work should be the same each time for tasks with no
conditional logic.


XXX shouldn't define SJ here and above.
\section system_jitter Defining System Jitter

XXX consider delaying mentioning histogram
This variation (or "jitter") in elapsed time can be measured and
displayed in a histogram.  This code aims to measure that jitter
so that a system can be tuned to minimize it.  Reducing jitter in
elapsed time measurements made with the TSC minimizes jitter
throughout the system.  Reading the TSC value is a very low-level
operation.  It is independent of higher-level languages and libraries
like Ultra Messaging.  We define system jitter as the jitter
measured at the lowest-possible level at which software can be
written.  Higer-level jitter will be discussed below.

Jitter in elapsed time measurements for doing identical tasks is
also a measure of things distracting a CPU core from running a
thread.  Some runs of a task may take longer than others because
a CPU core is not able to finish the task without first doing
additional work.  Hardware interrupts are one possible source of
such additonal work, but it's best to think of interruptions more
broadly.  Anything that keeps a CPU core from executing at full
speed is a distraction that hurts performance and adds jitter.

The CPU scheduler in the OS can be a source of jitter if a thread
is moved between CPU cores since cache misses in the new core will
initially slow execution compared to the old core.  Virtual machine
hypervisors may prevent a thread from holding the attention of a
CPU core indefinitely.  Even things at the CPU hardware level like
shared cache contention and hyperthreading can add jitter.


\section strategy Measurement Strategy

Jitter is most apparent when we measure the elapsed time taken to
do quick tasks.  The limiting case is to measure the time needed
to just read the TSC.  Measuring the delta between two adjacent
readings of the TSC does this.  It is equivalent to measuring
the elapsed time taken by a null task.

So our strategy for measuring system jitter is to measure the variance
in the elapsed time between two adjacent TSC readings.


\section application_jitter Contrast with Application Jitter

SJ Test uses no network I/O, no messaging library, no
memory-managed language, or abstraction layers.  It's measuring
jitter as close to the machine hardware as we know how to get with
C and in-line assembler.  Said differently, jitter measured here
is a coming from server hardware, BIOS, CPU, VM, and OS sources.

Adding networking, messaging libraries, memory-managed language
wrappers, and/or abstraction layers will only magnify the effects
of the jitter measured here.  The jitter values measured here are
a lower bound on values you might expect from measurements made
higher up the stack.

Note that we use the term "jitter" here in the general sense meaning
variation from an expected norm.  The system jitter discussed
here is just one component of the latency variation observed in
higher-level tests like repeated message round-trip time tests.
The standard deviation of latency measured in message round-trip
times is often loosely called "jitter," but we're disussing only
one component of that here.  Such tests must take timestamps using
RDTSC or some similar means, so the application jitter they measure
would include any system jitter measured here.


\section uses Target Uses

Informatica makes this source code freely available so that our
customers can use it to work with their server hardware and OS
vendors to reduce jitter.  We pioneered this idea with our mtools
software for testing multicast UDP performance.  Many customers
have used mtools to work with their NIC, driver, OS, and server
vendors to improve UDP multicast performance.  We hoipe that this
code can be used between our customers and their other vendors to
reduce system jitter.

XXX Goals: 1) Make measurements to quantify system jitter.
These measurements can be used as benchmarks
for performance comparisons between systems or to guide tuning efforts.
2) Provide a visualization of collected data that aids in analysis
and characterization of system jitter.
The visualization can help drive jitter tuning efforts.

It is sometimes possible to find the source of jitter based on its
magnitude, but most often in our experience, removing the jitter
is a process of trial and error.  Hence SJ Test was designed
to produce quick results so that the effect of changes could be
tested quickly.

XXX Value information density, but this is at odds with low res display

XXX introduce histogram as visualization tool here.

XXX Maybe contrast goals and constraints?

XXX Integrate this properly:
The histogram is displayed rotated 90 degrees clockwise from a
conventional display to keep the code portable across operating
systems and avoid the requirement for a high-resolution graphical
display. The histogram bins measuring time are displayed vertically
instead of the more conventional horizontal display. The count of
sample in each bin are displayed horizontally instead of the more
conventional vertical display.

Note that the histogram is mostly plotted non-linearly so it will
fit on low-resolution screens. Specifically, the number of counts
per bin is always displayed on as a logarithm. The magnitude (width)
of all bins is scaled so that the bin with the largest count uses
the full line width. The first half of the bins below the knee
setting are linearly spaced in the hope of catching the average
vast majority of the samples. The second half of the bins are spaced
roughly by half orders of magnitude so that every pair of bins
represents one order of magnitude.


\section requirements Requirements and Portability

This code requires an x86 processor for access to the RDTSC instruction.
It's with pre-compiled binaries for Linux, Solaris, Mac OS X, and FreeBSD.

 
\section histogram Histogram Bins and Knee Value

Good visualation of a histogram is aided by proper selection of
the bin values, while bin values are best selected with knowledge
of the expected distribution.  Although many natural processes
follow a normal distribution, the elapsed times measured in tests
like this are not expected to follow a normal distribution.
One reason is that there is a hard lower bound on the time required
to read the TSC so we shouldn't expect a symmetric or uniform
distribution around the mean.  Indeed in most cases, the mean is
quite close to the lower bound.  Another reason is that the different
processes that introduce jitter produce multimodal distributions.

We often see two or more closely-spaced modes near the lower bound
and then several more widely-spaced modes that are perhaps orders
of magnitude greater.  A high-resolution display might be able to
distinctly show both closely- and widely-spaced modes with consistent
scaling, but it's hard to write portable portable code for
high-resolution displays.

The compromise here is to write easily portable code that uses one
line of text output per histogram bin.  This limmits the practical
range of bins to a few dozen, meaning that consistent bin spacing
would obscure modes somewhere in the distribution.  We make it
easier for modes to be seen by using two different bin spacings
in the same histogram.  The lower half of the bins are linerally
spaced while the upper half are exponentially spaced.  This provides
detail near the lower bound where modes are closely spaced, while
preserving the range needed to show modes that occur 5 or more
orders of magnitude above the mean.  For linguistic convenience,
the bins in the lower half of the histogram are said to be "below
the knee," while those in the upper half are above.

Command line options allow setting the number of bins, the knee
value, and a minumum value which is the upper bound of the first
bin.
<tt>
 Example for 20 histogram bins with a minimum value of 0:
 Bin		Range
 0		      0- 10% * knee
 1		     10- 20% * knee
 2		     20- 30% * knee
 3		     30- 40% * knee
 4		     40- 50% * knee
 5		     50- 60% * knee
 6		     60- 70% * knee
 7		     70- 80% * knee
 8		     80- 90% * knee
 9		     90-100% * knee
 -		-------------------
 10		         1-2 * knee
 11		        2-10 * knee
 12		       10-20 * knee
 13		      20-100 * knee
 14		     100-200 * knee
 15		    200-1000 * knee
 16		   1000-2000 * knee
 17		  2000-10000 * knee
 18		 10000-20000 * knee
 19		20000-100000 * knee
</tt>


\section output Output Explanation

The first output section is the histogram.
It is divided into two halves, above and below the knee as
described above.
The data is presented in numerical and graphical form.
The first column shows the upper bound of each histogram bin in
terms of elapsed time between adjacent reads of the TSC.
The second column shows the same value but in counts of the TSC.
The third column is a count of the number of samples in the bin.
The fourth column is the percentage of all counts in the bin.
The fifth column is the cumulative percentage of all counts.
The sixth column is a graph of the count from the third column.

Counts will often vary from the many millions to single digits, so
the detail would get lost if they were graphed linerally.
Instead, the graph column shows the logarithm of the bin count.

The values in the graph column are scaled so that the bin with the
most counts will will have a line width equal to the maximum (default
80).

If you want to see more detail in the histogram and have a window with
many rows, try increasing the number of bins.
Or try increasing the output line width if you have a window with more
than 80 columns.


\section logging Logging and Plotting Outliers

An outlier is defined as any TSC delta > the knee.  The <tt>-f</tt> option
names a file where outliers will be written.  The format is x, y
where x is the time of the outlier in ms relative to the start of
the test and y is the size of the outlier in us.  Note different
units between axes.

The expectation is that you'll give this data to the graphing
software of your choice and request an x y scatter plot.  Visual
analysis of the plot may help you spot any periodic patterns in
the jitter.  The period may give you clues to the source of the
jitter.

Better yet, do an FFT on the data to move it from the time domain
to the frequency domain.

Note that x may be nowhere near zero if the outlier buffer wraps
around.  If you're worried about the outlier buffer wrapping around,
my advice is to increase the knee to classify fewer deltas as
outliers rather than making the buffer bigger.  The default size
is probalby big enough for you to spot any periodic patterns.


\section options Comand Line Options

<tt>
 -b bins	Set the number of Bins in the histogram (20)
 -f outfile	Name of file for outlier data to be written (no file written)
 -h		Print Help
 -k knee	Set the histogram Knee value in TSC ticks (50)
 -m min		Set the Minimum expected value in TSC ticks (30)
 -o outbuf	Size of outlier buffer in outliers (10000)
 -p pause	Pause msecs just before starting jitter test (0)
 -r runtime	Run jitter testing loops until seconds pass (1)
 -w width	Output line Width in characters (80)
 </tt>

XXX example output
This data was taken on my laptop, so it is not a model of great
server performance. About 62% of the samples are 15 ns, while less
than 0.5% took more than 30.1 ns. Less than 0.0022% took more than
60.2 ns. The minimum was 15 ns, while the worst case was one sample
that took 26.4 ms. The average was 18 ns with a standard deviation
of 2.25 µs.

XXX paste in output capture

This show output from one of our lab machines that has been tuned
to remove most system jitter.

The vast majority of all samples (99.9997%) are in the narrow range
from 11ns to 18ns. The worst case was only one sample at 4µs.

XXX paste in output capture

\section XXXfixMe Possible system jitter contrasts
Contrasts
Without VM, with VM
Multi-user, single-user
Linux Intel, Solaris AMD
Sprint, marathon
Min too high
Too few bins
Without pause, with pause
Periodic latency spikes
Without taskset, with taskset



\section copyright Copyright

(c) Copyright 2011, 2012 Informatica Corp.
Bob Van Valzah, October 2011, February 2012, October 2012


\section acknowledgement Acknowledgement

Inspired by David Riddoch of Solarflare


\section license License

Redistribution and use in source and binary forms, with or without
modification, are permitted without restriction.  The author will
be disappointed if you use this software for making war, mulching
babies, or improving systems running competing messaging software.


\section disclaimers Discalimers

THE SOFTWARE IS PROVIDED "AS IS" AND INFORMATICA DISCLAIMS ALL
WARRANTIES EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS
FOR A PARTICULAR PURPOSE.  INFORMATICA DOES NOT WARRANT THAT USE
OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.  INFORMATICA
SHALL NOT, UNDER ANY CIRCUMSTANCES, BE LIABLE TO LICENSEE FOR LOST
PROFITS, CONSEQUENTIAL, INCIDENTAL, SPECIAL OR INDIRECT DAMAGES
ARISING OUT OF OR RELATED TO THIS AGREEMENT OR THE TRANSACTIONS
CONTEMPLATED HEREUNDER, EVEN IF INFORMATICA HAS BEEN APPRISED OF
THE LIKELIHOOD OF SUCH DAMAGES.

*/
