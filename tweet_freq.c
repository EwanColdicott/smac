/*
Copyright (C) 2012 Paul Gardner-Stephen
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
  Compress short strings using english letter, bigraph, trigraph and quadgraph
  frequencies.  
  
  This part of the process only cares about lower-case letter sequences.  Encoding
  of word breaks, case changes and non-letter characters will be dealt with by
  separate layers.

  Interpolative coding is probably a good choice for a component in those higher
  layers, as it will allow efficient encoding of word break positions and other
  items that are possibly "clumpy"
*/

#include <stdio.h>
#include <strings.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "arithmetic.h"

extern unsigned int tweet_freqs3[69][69][69];
extern unsigned int tweet_freqs2[69][69];
extern unsigned int tweet_freqs1[69];
extern unsigned int caseend1[1][1];
extern unsigned int caseposn2[2][80][1];
extern unsigned int caseposn1[80][1];
extern unsigned int casestartofmessage[1][1];
extern unsigned int casestartofword2[2][1];
extern unsigned int casestartofword3[2][2][1];
extern unsigned int messagelengths[1024];
extern int wordCount;
extern char *wordList[];
extern unsigned int wordFrequencies[];
extern unsigned int wordSubstitutionFlag[1];

unsigned char chars[69]="abcdefghijklmnopqrstuvwxyz 0123456789!@#$%^&*()_+-=~`[{]}\\|;:'\"<,>.?/";
int charIdx(unsigned char c)
{
  int i;
  for(i=0;i<69;i++)
    if (c==chars[i]) return i;
       
  /* Not valid character -- must be encoded separately */
  return -1;
}
unsigned char wordChars[36]="abcdefghijklmnopqrstuvwxyz0123456789";
int charInWord(unsigned c)
{
  int i;
  int cc=tolower(c);
  for(i=0;wordChars[i];i++) if (cc==wordChars[i]) return 1;
  return 0;
}

double entropy3(int c1,int c2, char *string)
{
  int i;
  range_coder *t=range_new_coder(1024);
  for(i=0;i<strlen(string);i++)
    {
      int c3=charIdx(string[i]);
      range_encode_symbol(t,tweet_freqs3[c1][c2],69,c3);    
      //      printf("entropy after %c = %f\n",string[i],t->entropy);
      c1=c2; c2=c3;
    }
  double entropy=t->entropy;
  range_coder_free(t);
  return entropy;
}

double entropyOfInverse(int n)
{
  return -log(1.0/n)/log(2);
}

int encodeLCAlphaSpace(range_coder *c,unsigned char *s)
{
  int c1=charIdx(' ');
  int c2=charIdx(' ');
  int o;
  for(o=0;s[o];o++) {
    int c3=charIdx(s[o]);
    
    if (!charInWord(s[o-1])) {
      /* We are at a word break, so see if we can do word substitution.
	 Either way, we must flag whether we performed word substitution */
      int w;
      int longestWord=-1;
      int longestLength=0;
      double longestSavings=0;
      if (charInWord(s[o])) {
#if 0
	{
	  /* See if it makes sense to encode part of the message from here
	     without using 3rd order model. */
	  range_coder *t=range_new_coder(2048);
	  range_coder *tf=range_new_coder(1024);
	  // approx cost of switching models twice
	  double entropyFlat=10+entropyOfInverse(69+1+1);
	  int i;
	  int cc2=c2;
	  int cc1=c1;
	  for(i=o;s[i]&&(isalnum(s[i])||s[i]==' ');i++) {
	    int c3=charIdx(s[i]);
	    range_encode_symbol(t,tweet_freqs3[cc1][cc2],69,c3);
	    range_encode_equiprobable(tf,69+1,c3);
	    if (!s[i]) {
	      /* encoding to the end of message saves us the 
		 stop symbol */
	      tf->entropy-=entropyOfInverse(69+1);
	    }
	    if ((t->entropy-tf->entropy-entropyFlat)>longestSavings) {
	      longestLength=i-o+1;
	      longestSavings=t->entropy-tf->entropy-entropyFlat;
	    }
	    cc1=cc2; cc2=c3;
	  }

	  if (longestLength>0)
	    printf("Could save %f bits by flat coding next %d chars.\n",
		   longestSavings,longestLength);
	  else
	    printf("No saving possible from flat coding.\n");
	  longestSavings=0; longestLength=0;
	  range_coder_free(t);
	  range_coder_free(tf);
	}
#endif

	for(w=0;w<wordCount;w++) {
	  if (!strncmp((char *)&s[o],wordList[w],strlen(wordList[w]))) {
	    if (0) printf("    word match: strlen=%d, longestLength=%d\n",
			  (int)strlen(wordList[w]),(int)longestLength
			  );
	    double entropy=entropy3(c1,c2,wordList[w]);
	    range_coder *t=range_new_coder(1024);
	    range_encode_symbol(t,wordSubstitutionFlag,2,0);
	    range_encode_symbol(t,wordFrequencies,wordCount,w);
	    double substEntropy=t->entropy;
	    range_coder_free(t);
	    double savings=entropy-substEntropy;
	    
	    if (strlen(wordList[w])>longestLength) {
	      longestLength=strlen(wordList[w]);
	      longestWord=w;	      
	      if (0)
		printf("spotted substitutable instance of '%s' -- save %f bits (%f vs %f)\n",
		     wordList[w],savings,substEntropy,entropy);
	    }
	  }
	}
      }
      if (longestWord>-1) {
	/* Encode "we are substituting a word here */
	double entropy=c->entropy;
	range_encode_symbol(c,wordSubstitutionFlag,2,0);

	/* Encode the word */
	range_encode_symbol(c,wordFrequencies,wordCount,longestWord);
	
	printf("substituted %s at a cost of %f bits.\n",
		 wordList[longestWord],c->entropy-entropy);

	/* skip rest of word, but make sure we stay on track for 3rd order model
	   state. */
	o+=longestLength-1;
	c3=charIdx(s[o-1]);
	c2=charIdx(s[o]);
	continue;
      } else {
	/* Encode "not substituting a word here" symbol */
	double entropy=c->entropy;
	range_encode_symbol(c,wordSubstitutionFlag,2,1);
	if (0)
	  printf("incurring non-substitution penalty = %f bits\n",
		 c->entropy-entropy);
      }
    }
    range_encode_symbol(c,tweet_freqs3[c1][c2],69,c3);    
    c1=c2; c2=c3;
  }
  return 0;
}

int encodeLength(range_coder *c,unsigned char *m)
{
  int len=strlen((char *)m);

  range_encode_symbol(c,messagelengths,1024,len);

  return 0;
}

int encodeNonAlpha(range_coder *c,unsigned char *m)
{
  /* Get positions and values of non-alpha chars.
     Encode count, then write the chars, then use interpolative encoding to
     encode their positions. */

  unsigned int probNoNonAlpha=0.95*0xffffffff;

  char v[1024];
  int pos[1024];
  int count=0;
  
  int i;
  for(i=0;m[i];i++)
    if (charIdx(tolower(m[i]))>=0) {
      /* alpha or space -- so ignore */
    } else {
      /* non-alpha, so remember it */
      v[count]=m[i];
      printf("non-alpha char: 0x%02x '%c'\n",m[i],m[i]);
      pos[count++]=i;
    }

  // XXX - The following assumes that 50% of messages have special characters.
  // This is a patently silly assumption.
  if (!count) {
    // printf("Using 1 bit to indicate no non-alpha/space characters.\n");
    range_encode_symbol(c,&probNoNonAlpha,2,0);
    return 0;
  } else {
    // There are special characters present 
    range_encode_symbol(c,&probNoNonAlpha,2,1);
  }

  printf("Using 8-bits to encode each of %d non-alpha chars.\n",count);

  /* Encode number of non-alpha chars using:
     n 1's to specify how many bits required to encode the count.
     Then 0.
     Then n bits to encode the count.
     So 2*ceil(log2(count))+1 bits
  */

  int len=strlen((char *)m);
  range_encode_length(c,len);  

  // printf("Using %f bits to encode the number of non-alpha/space chars.\n",countBits);

  /* Encode the positions of special characters */
  ic_encode_heiriter(pos,count,NULL,NULL,len,len,c);
  
  /* Encode the characters */
  for(i=0;i<count;i++) {
    range_encode_equiprobable(c,256,v[1]); 
  }

  // printf("Using interpolative coding for positions, total = %d bits.\n",posBits);

  return 0;
}

int mungeCase(char *m)
{
  int i;

  /* Change isolated I's to i, provided preceeding char is lower-case
     (so that we don't mess up all-caps).
  */
  for(i=1;m[i+1];i++)
    if (m[i]=='I'&&(!isalpha(m[i-1]))&&(!isalpha(m[i+1])))
      {
	m[i]^=0x20;
      }
     
  return 0;
}

double encodeCaseModel1(range_coder *c,unsigned char *line)
{
  /*
    Have previously looked at flipping case of isolated I's 
    first, but makes only 1% difference in entropy of case,
    which doesn't seem enough to give confidence that it will
    typically help.  This might change if we model probability of
    case of first letter of a word based on case of first letter of
    previous word.
  */

  int wordNumber=0;
  int wordPosn=-1;
  int lastWordInitialCase=0;
  int lastWordInitialCase2=0;
  int lastCase=0;

  int i;
  //  printf("caps eligble chars: ");
  for(i=0;line[i];i++) {
    int wordChar=charInWord(line[i]);
    if (!wordChar) {	  
      wordPosn=-1; lastCase=0;
    } else {
      if (isalpha(line[i])) {
	if (wordPosn<0) wordNumber++;
	wordPosn++;
	int upper=0;
	int caseEnd=0;
	if (isupper(line[i])) upper=1;
	/* note if end of word (which includes end of message,
	   implicitly detected here by finding null at end of string */
	if (!charInWord(line[i+1])) caseEnd=1;
	if (wordPosn==0) {
	  /* first letter of word, so can only use 1st-order model */
	  unsigned int frequencies[1]={caseposn1[0][0]};
	  if (i==0) frequencies[0]=casestartofmessage[0][0];
	  else if (wordNumber>1&&wordPosn==0) {
	    /* start of word, so use model that considers initial case of
	       previous word */
	    frequencies[0]=casestartofword2[lastWordInitialCase][0];
	    if (wordNumber>2)
	      frequencies[0]=
		casestartofword3[lastWordInitialCase2][lastWordInitialCase][0];
	    if (0)
	      printf("last word began with case=%d, p_lower=%f\n",
		     lastWordInitialCase,
		     (frequencies[0]*1.0)/0x100000000
		     );
	  }
	  if (0) printf("case of first letter of word/message @ %d: p=%f\n",
			i,(frequencies[0]*1.0)/0x100000000);
	  range_encode_symbol(c,frequencies,2,upper);
	} else {
	  /* subsequent letter, so can use case of previous letter in model */
	  if (wordPosn>79) wordPosn=79;
	  if (0) printf("case of first letter of word/message @ %d.%d: p=%f\n",
			i,wordPosn,
			(caseposn2[lastCase][wordPosn][0]*1.0)/0x100000000);
	  range_encode_symbol(c,caseposn2[lastCase][wordPosn],2,upper);
	}
	if (isupper(line[i])) lastCase=1; else lastCase=0;
	if (wordPosn==0) {
	  lastWordInitialCase2=lastWordInitialCase;
	  lastWordInitialCase=lastCase;
	}
      }
    }
    
    /* fold all letters to lower case */
    if (line[i]>='A'&&line[i]<='Z') line[i]|=0x20;
  }
  //  printf("\n");

  return 0;
}

int stripNonAlpha(unsigned char *in,unsigned char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++)
    if (charIdx(tolower(in[i]))>=0) out[l++]=in[i];
  out[l]=0;
  return 0;
}

int stripCase(unsigned char *in,unsigned char *out)
{
  int l=0;
  int i;
  for(i=0;in[i];i++) out[l++]=tolower(in[i]);
  out[l]=0;
  return 0;
}

int freq_compress(range_coder *c,unsigned char *m)
{
  unsigned char alpha[1024]; // message with all non alpha/spaces removed
  unsigned char lcalpha[1024]; // message with all alpha chars folded to lower-case

  unsigned int probPackedASCII=0.95*0xffffffff;

  /* Use model instead of just packed ASCII */
  range_encode_equiprobable(c,2,1); // not raw ASCII
  range_encode_symbol(c,&probPackedASCII,2,0); // not packed ASCII

  printf("%f bits to encode model\n",c->entropy);
  double lastEntropy=c->entropy;
  
  /* Encode length of message */
  encodeLength(c,m);
  
  printf("%f bits to encode length\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;

  /* encode any non-ASCII characters */
  encodeNonAlpha(c,m);
  stripNonAlpha(m,alpha);

  printf("%f bits to encode non-alpha\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;

  /* compress lower-caseified version of message */
  stripCase(alpha,lcalpha);
  encodeLCAlphaSpace(c,lcalpha);

  printf("%f bits to encode chars\n",c->entropy-lastEntropy);
  lastEntropy=c->entropy;
  
  /* case must be encoded after symbols, so we know how many
     letters and where word breaks are.
 */
  mungeCase((char *)alpha);
  encodeCaseModel1(c,alpha);
  
  printf("%f bits to encode case\n",c->entropy-lastEntropy);

  range_conclude(c);
  printf("%d bits actually used after concluding.\n",c->bits_used);

  if (c->bits_used>=7*strlen((char *)m))
    {
      /* we can't encode it more efficiently than 7-bit ASCII */
      range_coder_reset(c);
      range_encode_equiprobable(c,2,1); // not raw ASCII
      range_encode_symbol(c,&probPackedASCII,2,1); // is packed ASCII
      int i;
      for(i=0;m[i];i++) {
	int v=m[i];
	int upper=0;
	if (isalpha(v)&&isupper(v)) upper=1;
	v=tolower(v);
	v=charIdx(v);
	range_encode_equiprobable(c,69,v);
	if (isalpha(v))
	  range_encode_equiprobable(c,2,upper);
      }
      range_conclude(c);
      printf("Reverting to raw non-statistical encoding: %d chars in 2+%f bits\n",
	     (int)strlen((char *)m),c->entropy-2.0);
    }
  
  if ((c->bits_used>=8*strlen((char*)m))
      &&(!(m[0]&0x80)))
    {
      /* we can't encode it more efficiently than 8-bit raw.
         We can only do this is MSB of first char of message is 0, as we use
	 the first bit of the message to indicate if it is compressed or not. */
      int i;
      range_coder_reset(c);
      for(i=0;m[i];i++) c->bit_stream[i]=m[i];
      c->bits_used=8*i;
      c->entropy=8*i;
      printf("Reverting to raw 8-bit encoding: used %d bits\n",c->bits_used);
    }

  return 0;
}


int main(int argc,char *argv[])
{
  if (!argv[1]) {
    fprintf(stderr,"Must provide message to compress.\n");
    exit(-1);
  }

  char m[1024]; // raw message, no pre-processing
  
  FILE *f;

  if (strcmp(argv[1],"-")) f=fopen(argv[1],"r"); else f=stdin;
  if (!f) {
    fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
    exit(-1);
  }

  m[0]=0; fgets(m,1024,f);
  
  int lines=0;
  double runningPercent=0;
  double worstPercent=0,bestPercent=100;

  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;
    if (1) printf(">>> %s\n",m);

    range_coder *c=range_new_coder(1024);
    freq_compress(c,(unsigned char *)m);

    double percent=c->bits_used*100.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;
    runningPercent+=percent;

    lines++;

    printf("Total encoded length = %d bits = %.2f%% (best:avg:worst %.2f%%:%.2f%%:%.2f%%)\n",
	   c->bits_used,percent,bestPercent,runningPercent/lines,worstPercent);
    m[0]=0; fgets(m,1024,f);
  }
  fclose(f);
  return 0;
}
