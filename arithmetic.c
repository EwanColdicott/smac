#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
#include "arithmetic.h"

#undef UNDERFLOWFIX

int range_decode_getnextbit(range_coder *c)
{
  /* return 0s once we have used all bits */
  if (c->bit_stream_length<(c->bits_used)) return 0;

  int bit=c->bit_stream[c->bits_used>>3]&(1<<(7-(c->bits_used&7)));
  c->bits_used++;
  if (bit) return 1;
  return 0;
}

int range_emitbit(range_coder *c,int b)
{
  if (c->bits_used>=(c->bit_stream_length)) {
    printf("out of bits\n");
    return -1;
}
  int bit=(c->bits_used&7)^7;
  if (b) c->bit_stream[c->bits_used>>3]|=(b<<bit);
  else c->bit_stream[c->bits_used>>3]&=~(b<<bit);
  c->bits_used++;
  return 0;
}

int range_emit_stable_bits(range_coder *c)
{

  while(1) {

  /* look for actually stable bits */
  if (!((c->low^c->high)&0x80000000))
    {
      int msb=c->low>>31;
      if (range_emitbit(c,msb)) return -1;
      if (c->underflow) {
	int u;
	if (msb) u=0; else u=1;
	while (c->underflow-->0) if (range_emitbit(c,u)) return -1;
	c->underflow=0;
      }
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }
#ifdef UNDERFLOWFIX
  /* Now see if we have underflow, and need to count the number of underflowed
     bits. */
  else if (((c->low&0xc0000000)==0x40000000)
	   &&((c->high&0xc0000000)==0x80000000))
    {
      c->underflow++;
      c->low=(c->low&0x80000000)|((c->low<<1)&0x7fffffff);
      c->high=(c->high&0x80000000)|((c->high<<1)&0x7fffffff);
      c->high|=1;
    }
#endif
  else 
    return 0;
  }
  return 0;
}

int range_emitbits(range_coder *c,int n)
{
  int i;
  for(i=0;i<n;i++)
    {
      if (range_emitbit(c,(c->low>>31))) return -1;
      c->low=c->low<<1;
      c->high=c->high<<1;
      c->high|=1;
    }
  return 0;
}


char bitstring[33];
char *asbits(unsigned int v)
{
  int i;
  bitstring[32]=0;
  for(i=0;i<32;i++)
    if ((v>>(31-i))&1) bitstring[i]='1'; else bitstring[i]='0';
  return bitstring;
}

int range_encode(range_coder *c,double p_low,double p_high)
{
  if (p_low>=p_high) {
    fprintf(stderr,"range_encode() called with p_low>=p_high: p_low=%f, p_high=%f\n",
	    p_low,p_high);
    exit(-1);
  }

  unsigned int space=c->high-c->low;
  if (space<0x10000) {
    fprintf(stderr,"Ran out of room in coder space (convergence around 0.5?)\n");
    exit(-1);
  }
  double new_low=c->low+p_low*space;
  double new_high=c->low+p_high*space;

  c->low=new_low;
  c->high=new_high;

  c->entropy+=-log(p_high-p_low)/log(2);

  if (range_emit_stable_bits(c)) return -1;
  return 0;
}

int range_status(range_coder *c)
{
  printf("range=[%s,",asbits(c->low));
  printf("%s), ",asbits(c->high));
  printf("value=%s\n",asbits(c->value));
 

  return 0;
}

/* No more symbols, so just need to output enough bits to indicate a position
   in the current range */
int range_conclude(range_coder *c)
{
  int bits=0;
  unsigned int v;
  unsigned int mean=((c->high-c->low)/2)+c->low;

  /* wipe out hopefully irrelevant bits from low part of range */
  v=0;
  while((v<=c->low)||(v>=c->high))
    {
      bits++;
      v=(mean>>(32-bits))<<(32-bits);
    }
  /* Actually, apparently 2 bits is always the correct answer, because normalisation
     means that we always have 2 uncommitted bits in play, excepting for underflow
     bits, which we handle separately. */
  if (bits<2) bits=2;
  c->value=mean;
  printf("%d bits to conclude. ",bits);
  range_status(c);
  c->value=0;

  int i,msb=(v>>31)&1;

  /* output msb and any deferred underflow bits. */
  printf("emit msb %d\n",msb);
  if (range_emitbit(c,msb)) return -1;
  if (c->underflow>0) printf("  plus %d underflow bits.\n",c->underflow);
  while(c->underflow-->0) if (range_emitbit(c,msb^1)) return -1;

  /* now push bits until we know we have enough to unambiguously place the value
     within the final probability range. */
  for(i=1;i<bits;i++) {
    int b=(v>>(31-i))&1;
    printf("emit %d\n",b);
    if (range_emitbit(c,b)) return -1;
  }

  return 0;
}

int range_coder_reset(struct range_coder *c)
{
  c->low=0;
  c->high=0xffffffff;
  c->entropy=0;
  c->bits_used=0;
  return 0;
}

struct range_coder *range_new_coder(int bytes)
{
  struct range_coder *c=calloc(sizeof(struct range_coder),1);
  c->bit_stream=malloc(bytes);
  c->bit_stream_length=bytes*8;
  range_coder_reset(c);
  return c;
}



/* Assumes probabilities are cumulative */
int range_encode_symbol(range_coder *c,double frequencies[],int alphabet_size,int symbol)
{
  double p_low=0;
  if (symbol>0) p_low=frequencies[symbol-1];
  double p_high=1;
  if (symbol<(alphabet_size-1)) p_high=frequencies[symbol];
  // printf("symbol=%d, p_low=%f, p_high=%f\n",symbol,p_low,p_high);
  return range_encode(c,p_low,p_high);
}

int range_check(range_coder *c,int line)
{
  if (c->value>c->high||c->value<c->low) {
    fprintf(stderr,"c->value out of bounds %d\n",line);
    range_status(c);
    exit(-1);
  }
  return 0;
}

int range_decode_symbol(range_coder *c,double frequencies[],int alphabet_size)
{
  int s;
  double space=c->high-c->low;
  double v=(c->value-c->low)/space;
  
  //  printf(" decode: v=%f; ",v);
  // range_status(c);

  for(s=0;s<(alphabet_size-1);s++)
    if (v<frequencies[s]) break;
  
  double p_low=0;
  if (s>0) p_low=frequencies[s-1];
  double p_high=1;
  if (s<alphabet_size-1) p_high=frequencies[s];

  // printf("s=%d, v=%f, p_low=%f, p_high=%f\n",s,v,p_low,p_high);
  // range_status(c);

  double new_low=c->low+p_low*space;
  double new_high=c->low+p_high*space;

  /* work out how many bits are still significant */
  c->low=new_low;
  c->high=new_high;

  // printf("after decode before renormalise: ");
  // range_status(c);

  while(1) {
    if ((c->low&0x80000000)==(c->high&0x80000000))
      {
	/* MSBs match, so bit will get shifted out */
      }
#if UNDERFLOWFIX
    else if (((c->low&0xc0000000)==0x40000000)
	     &&((c->high&0xc0000000)==0x80000000))
      {
	c->value^=0x40000000;
	c->low&=0x3fffffff;
	c->high|=0x40000000;
      }
#endif
    else {
      /* nothing can be done */
      range_check(c,__LINE__);
      return s;
    }

    c->low=c->low<<1;
    c->high=c->high<<1;
    c->high|=1;
    c->value=c->value<<1; 
    c->value|=range_decode_getnextbit(c);
    if (c->value>c->high||c->value<c->low) {
      fprintf(stderr,"c->value out of bounds %d\n",__LINE__);
      exit(-1);
    }
  }
  range_check(c,__LINE__);
  return s;
}

int range_decode_prefetch(range_coder *c)
{
  c->low=0;
  c->high=0xffffffff;
  c->value=0;
  int i;
  for(i=0;i<32;i++)
    c->value=(c->value<<1)|range_decode_getnextbit(c);
  return 0;
}

int cmp_double(const void *a,const void *b)
{
  double *aa=(double *)a;
  double *bb=(double *)b;

  if (*aa<*bb) return -1;
  if (*aa>*bb) return 1;
  return 0;
}

range_coder *range_coder_dup(range_coder *in)
{
  range_coder *out=calloc(sizeof(range_coder),1);
  bcopy(in,out,sizeof(range_coder));
  out->bit_stream=malloc(in->bit_stream_length/8+1);
  bcopy(in->bit_stream,out->bit_stream,1+(out->bits_used/8));
  return out;
}

int range_coder_free(range_coder *c)
{
  free(c->bit_stream); c->bit_stream=NULL;
  bzero(c,sizeof(range_coder));
  free(c);
  return 0;
}

int main() {
  struct range_coder *c=range_new_coder(8192);

  double frequencies[1024];
  int sequence[1024];
  int alphabet_size;
  int length;

  int test,i,j;

  srandom(0);

  for(test=0;test<1024;test++)
    {
      /* Pick a random alphabet size */
      alphabet_size=1+random()%1023;
      alphabet_size=1+test%1023;
 
      /* Generate incremental probabilities.
         Start out with randomly selected probabilities, then sort them.
	 It may make sense later on to use some non-uniform distributions
	 as well.

	 We only need n-1 probabilities for alphabet of size n, because the
	 probabilities are fences between the symbols, and p=0 and p=1 are implied
	 at each end.
      */
      for(i=0;i<alphabet_size-1;i++)
	frequencies[i]=random()*1.0/(0x7fffffff);
      frequencies[alphabet_size-1]=0;

      qsort(frequencies,alphabet_size-1,sizeof(double),cmp_double);

      /* now generate random string to compress */
      length=1+random()%1023;
      for(i=0;i<length;i++) sequence[i]=random()%alphabet_size;
      
      printf("Test #%d : %d symbols, with %d symbol alphabet\n",
	     test,length,alphabet_size);
      {
	int k;
	fprintf(stderr,"symbol probability steps: ");
	for(k=0;k<alphabet_size-1;k++) fprintf(stderr," %f",frequencies[k]);
	fprintf(stderr,"\n");
      }

      /* Encode the random symbols */
      range_coder_reset(c);
      for(i=0;i<length;i++) {
	range_coder *dup=range_coder_dup(c);

	if (range_encode_symbol(c,frequencies,alphabet_size,sequence[i]))
	  {
	    fprintf(stderr,"Error encoding symbol #%d of %d (out of space?)\n",
		    i,length);
	    exit(-1);
	  }

	/* verify as we go, so that we can report any divergence that we
	   notice. */
	range_coder *vc=range_coder_dup(c);
	range_conclude(vc);
	/* Now convert encoder state into a decoder state */
	vc->bit_stream_length=vc->bits_used;
	vc->bits_used=0;
	range_decode_prefetch(vc);

	for(j=0;j<=i;j++) {
	  range_coder *vc2=range_coder_dup(vc);

	  if (j==i) {
	    printf("coder status before emitting symbol #%d:\n  ",i); 
	    range_status(dup);
	    printf("coder status after emitting symbol #%d:\n  ",i); 
	    range_status(c);
	    printf("decoder status before extracting symbol #%d:\n  ",i);
	    range_status(vc2);

	    int s;
	    double space=vc2->high-vc2->low;
	    double v=(vc2->value-vc2->low)/space;
	    
	    //  printf(" decode: v=%f; ",v);
	    // range_status(c);
	    
	    for(s=0;s<(alphabet_size-1);s++)
	      if (v<frequencies[s]) break;
	    
	    double p_low=0;
	    if (s>0) p_low=frequencies[s-1];
	    double p_high=1;
	    if (s<alphabet_size-1) p_high=frequencies[s];
	    
	    printf("  s=%d, v=%f, p_low=%f, p_high=%f\n",s,v,p_low,p_high);

	  }
	  
	  int s=range_decode_symbol(vc,frequencies,alphabet_size);
	  if (s!=sequence[j]) {
	    fflush(stdout);
	    fprintf(stderr,"Verify error decoding symbol %d of [0,%d] (expected %d, but got %d)\n",j,i,sequence[j],s);
	    if (j==i-1) {
	      fflush(stdout);
	      fprintf(stderr,"Verify is on final character.\n"
		      "Encoder state before encoding final symbol was as follows:\n");
	      fflush(stderr);
	    }

	    exit(-1);
	  }
	  range_coder_free(vc2);
	}

	range_coder_free(vc);
	range_coder_free(dup);
      }
      range_conclude(c);
      printf("  encoded %d symbols in %d bits (%f bits of entropy)\n",
	     length,c->bits_used,c->entropy);
      
      printf("  successfully decoded and verified %d symbols.\n",length);
    }

  return 0;
}
