CC=gcc
COPT=-g -Wall
CFLAGS=-g -Wall
DEFS=

OBJS=	main.o \
	\
	smaz.o \
	\
	method_stats3.o \
	\
	case.o \
	length.o \
	lowercasealpha.o \
	nonalpha.o \
	packedascii.o \
	packed_stats.o \
	\
	charset.o \
	entropyutil.o \
	message_stats.o \
	\
	arithmetic.o \
	gsinterpolative.o

HDRS=	charset.h arithmetic.h message_stats.h packed_stats.h Makefile

all: method_stats3 arithmetic gsinterpolative # smaz_test gen_stats tweet_stats.c

smaz_test: smaz_test.c smaz.c
	gcc -o smaz_test -O2 -Wall -W -ansi -pedantic smaz.c smaz_test.c

clean:
	rm -rf smaz_test *.o gen_stats method_stats3

arithmetic:	arithmetic.c arithmetic.h
# Build for running tests
	gcc $(CFLAGS) -DSTANDALONE -o arithmetic arithmetic.c

extract_tweets:	extract_tweets.o
	gcc $(CFLAGS) -o extract_tweets extract_tweets.o

gen_stats:	gen_stats.c arithmetic.o packed_stats.o gsinterpolative.o
	gcc $(CFLAGS) -o gen_stats gen_stats.c arithmetic.o packed_stats.o gsinterpolative.o

message_stats.c:	gen_stats twitter_corpus*.txt
	./gen_stats 5 3 twitter_corpus*.txt

method_stats3:	$(OBJS)
	gcc -g -Wall -o method_stats3 $(OBJS)

gsinterpolative:	gsinterpolative.c $(OBJS)
	gcc -g -Wall -DSTANDALONE -o gsinterpolative{,.c} arithmetic.o

%.o:	%.c $(HDRS)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

test:	gsinterpolative arithmetic
	./gsinterpolative
	./arithmetic
	./method_stats3 twitter_corpus*.txt
