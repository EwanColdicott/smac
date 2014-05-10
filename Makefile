CC=gcc
COPT=-g -Wall -O3
CFLAGS=-g -Wall -O3
LDFLAGS=-lm
DEFS=

OBJS=	main.o \
	\
	smac.o \
	\
	recipe.o \
	dexml.o \
	md5.o \
	\
	unicode.o \
	case.o \
	length.o \
	lowercasealpha.o \
	nonalpha.o \
	packedascii.o \
	packed_stats.o \
	\
	charset.o \
	entropyutil.o \
	\
	arithmetic.o \
	gsinterpolative.o \
	\
	visualise.o

HDRS=	charset.h arithmetic.h packed_stats.h unicode.h visualise.h recipe.h Makefile

all: smac arithmetic gsinterpolative gen_stats xml2recipe

clean:
	rm -rf gen_stats smac *.o gsinterpolative xml2recipe

arithmetic:	arithmetic.c arithmetic.h
# Build for running tests
	gcc $(CFLAGS) -DSTANDALONE -o arithmetic arithmetic.c $(LDFLAGS)

extract_tweets:	extract_tweets.o
	gcc $(CFLAGS) -o extract_tweets extract_tweets.o

gen_stats:	gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o
	gcc $(CFLAGS) -o gen_stats gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o $(LDFLAGS)

smac:	$(OBJS)
	gcc -g -Wall -o smac $(OBJS) $(LDFLAGS)

gsinterpolative:	gsinterpolative.c $(OBJS)
	gcc -g -Wall -DSTANDALONE -o gsinterpolative gsinterpolative.c arithmetic.o $(LDFLAGS)

%.o:	%.c $(HDRS)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

test:	gsinterpolative arithmetic
	./gsinterpolative
	./arithmetic
	./smac twitter_corpus*.txt

out.odt:	content.xml
	cp content.xml odt-shell/
	cd odt-shell ; zip -r ../out.odt *

extract_instance_with_library:	extract_instance_with_library.c Makefile
	$(CC) $(CFLAGS) -o extract_instance_with_library extract_instance_with_library.c -lexpat

xml2recipe:	xml2recipe.c Makefile
	$(CC) $(CFLAGS) -o xml2recipe xml2recipe.c -lexpat $(LDFLAGS)

