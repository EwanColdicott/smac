/*
  Compress a key value pair file according to a recipe.
  The recipe indicates the type of each field.
  For certain field types the precision or range of allowed
  values can be specified to further aid compression.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"

// min,max set inclusive bound
#define FIELDTYPE_INTEGER 0
// precision specifies bits of precision. currently only 32 is supported.
#define FIELDTYPE_FLOAT 1
// precision sets number of decimal places.
// gets encoded by multiplying value,min and max by 10^precision, and then encoding as an integer.
#define FIELDTYPE_FIXEDPOINT 2
#define FIELDTYPE_BOOLEAN 3
// precision is bits of time of day encoded.  17 = 1 sec granularity
#define FIELDTYPE_TIMEOFDAY 4
// precision is bits of date encoded.  32 gets full UNIX Julian seconds.
// 16 gets resolution of ~ 1 day.
// with min set appropriately, 25 gets 1 second granularity within a year.
#define FIELDTYPE_DATE 5
// precision is bits of precision in coordinates
#define FIELDTYPE_LATLONG 6
// min,max refer to size limits of text field.
// precision refers to minimum number of characters to encode if we run short of space.
#define FIELDTYPE_TEXT 7

int recipe_parse_fieldtype(char *name)
{
  if (!strcasecmp(name,"integer")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"int")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"float")) return FIELDTYPE_FLOAT;
  if (!strcasecmp(name,"fixedpoint")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"boolean")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"bool")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"timeofday")) return FIELDTYPE_TIMEOFDAY;
  if (!strcasecmp(name,"date")) return FIELDTYPE_DATE;
  if (!strcasecmp(name,"latlong")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"text")) return FIELDTYPE_TEXT;
  
  return -1;
}

struct field {
  char *name;
  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
};

struct recipe {
  struct field fields[1024];
  int field_count;
};

char recipe_error[1024]="No error.\n";

void recipe_free(struct recipe *recipe)
{
  int i;
  for(i=0;i<recipe->field_count;i++) {
    if (recipe->fields[i].name) free(recipe->fields[i].name);
    recipe->fields[i].name=NULL;
  }
  free(recipe);
}

struct recipe *recipe_read(char *buffer,int buffer_size)
{
  if (buffer_size<1||buffer_size>1048576) {
    snprintf(recipe_error,1024,"Recipe file empty or too large (>1MB).\n");
    return NULL;
  }

  struct recipe *recipe=calloc(sizeof(struct recipe),1);
  if (!recipe) {
    snprintf(recipe_error,1024,"Allocation of recipe structure failed.\n");
    return NULL;
  }

  int i;
  int l=0;
  int line_number=1;
  char line[1024];
  char name[1024],type[1024];
  int min,max,precision;

  for(i=0;i<=buffer_size;i++) {
    if (l>1000) { 
      snprintf(recipe_error,1024,"line:%d:Line too long.\n",line_number);
      recipe_free(recipe); return NULL; }
    if ((i==buffer_size)||(buffer[i]=='\n')||(buffer[i]=='\r')) {
      if (recipe->field_count>1000) {
	snprintf(recipe_error,1024,"line:%d:Too many field definitions (must be <=1000).\n",line_number);
	recipe_free(recipe); return NULL;
      }
      // Process recipe line
      line[l]=0; 
      if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^:]:%[^:]:%d:%d:%d",
		   name,type,&min,&max,&precision)==5) {
	  int fieldtype=recipe_parse_fieldtype(type);
	  if (fieldtype==-1) {
	    snprintf(recipe_error,1024,"line:%d:Unknown or misspelled field type '%s'.\n",line_number,type);
	    recipe_free(recipe); return NULL;
	  } else {
	    // Store parsed field
	    recipe->fields[recipe->field_count].name=strdup(name);
	    recipe->fields[recipe->field_count].type=fieldtype;
	    recipe->fields[recipe->field_count].minimum=min;
	    recipe->fields[recipe->field_count].maximum=max;
	    recipe->fields[recipe->field_count].precision=precision;
	    recipe->field_count++;
	  }
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed field definition.\n",line_number);
	  recipe_free(recipe); return NULL;
	}
      }
      line_number++; l=0;
    } else {
      line[l++]=buffer[i];
    }
  }
  return recipe;
}

struct recipe *recipe_read_from_file(char *filename)
{
  struct recipe *recipe=NULL;

  unsigned char *buffer;

  int fd=open(filename,O_RDONLY);
  if (fd==-1) {
    snprintf(recipe_error,1024,"Could not open recipe file '%s'\n",filename);
    return NULL;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    snprintf(recipe_error,1024,"Could not stat recipe file '%s'\n",filename);
    close(fd); return NULL;
  }

  buffer=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer==MAP_FAILED) {
    snprintf(recipe_error,1024,"Could not memory map recipe file '%s'\n",filename);
    close(fd); return NULL; 
  }

  recipe=recipe_read((char *)buffer,stat.st_size);

  munmap(buffer,stat.st_size);
  close(fd);
  
  if (recipe&&recipe->field_count==0) {
    recipe_free(recipe);
    snprintf(recipe_error,1024,"Recipe contains no field definitions\n");
    return NULL;
  }

  return recipe;
}

int recipe_encode_field(struct recipe *recipe,range_coder *c,
			int fieldnumber,char *value)
{
  return -1;
}

int recipe_decompress(struct recipe *recipe,unsigned char *in,int in_len, char *out, int out_size)
{
  snprintf(recipe_error,1024,"recipe_decompress() is not implemented\n");
  return -1;
}

int recipe_compress(struct recipe *recipe,char *in,int in_len, unsigned char *out, int out_size)
{
  /*
    Eventually we want to support full skip logic, repeatable sections and so on.
    For now we will allow skip sections by indicating missing fields.
    This approach lets us specify fields implictly by their order in the recipe
    (NOT in the completed form).
    This entails parsing the completed form, and then iterating through the RECIPE
    and considering each field in turn.  A single bit per field will be used to
    indicate whether it is present.  This can be optimised later.
  */

  if (!recipe) {
    snprintf(recipe_error,1024,"No recipe provided.\n");
    return -1;
  }
  if (!in) {
    snprintf(recipe_error,1024,"No input provided.\n");
    return -1;
  }
  if (!out) {
    snprintf(recipe_error,1024,"No output buffer provided.\n");
    return -1;
  }

  // Make new range coder with 1KB of space
  range_coder *c=range_new_coder(8192);
  if (!c) {
    snprintf(recipe_error,1024,"Could not instantiate range coder.\n");
    return -1;
  }

  char *keys[1024];
  char *values[1024];
  int value_count=0;

    int i;
  int l=0;
  int line_number=1;
  char line[1024];
  char key[1024],value[1024];

  for(i=0;i<=in_len;i++) {
    if (l>1000) { 
      snprintf(recipe_error,1024,"line:%d:Data line too long.\n",line_number);
      return -1; }
    if ((i==in_len)||(in[i]=='\n')||(in[i]=='\r')) {
      if (value_count>1000) {
	snprintf(recipe_error,1024,"line:%d:Too many data lines (must be <=1000).\n",line_number);
	return -1;
      }
      // Process key=value line
      line[l]=0; 
      if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^=]=%s",key,value)==2) {
	  keys[value_count]=strdup(key);
	  values[value_count]=strdup(value);
	  value_count++;
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed data line.\n",line_number);
	  return -1;
	}
      }
      line_number++; l=0;
    } else {
      line[l++]=in[i];
    }
  }
  printf("Read %d data lines.\n",value_count);

  int field;

  for(field=0;field<recipe->field_count;field++) {
    // look for this field in keys[] 
    for (i=0;i<value_count;i++) {
      if (!strcasecmp(keys[i],recipe->fields[field].name)) break;
    }
    if (i<value_count) {
      // Field present
      printf("Found field #%d ('%s')\n",field,recipe->fields[field].name);
      // Record that the field is present.
      range_encode_equiprobable(c,2,1);
      // Now, based on type of field, encode it.
      if (recipe_encode_field(recipe,c,field,values[i]))
	{
	  range_coder_free(c);
	  snprintf(recipe_error,1024,"Could not record value '%s' for field '%s'\n",
		   values[i],recipe->fields[field].name);
	  return -1;
	}
    } else {
      // Field missing: record this fact and nothing else.
      range_encode_equiprobable(c,2,0);
    }
  }

  range_conclude(c);
  printf("Used %d bits.\n",c->bits_used);
  range_coder_free(c);

  snprintf(recipe_error,1024,"recipe_compress() is not implemented\n");
  return -1;

}

int recipe_compress_file(char *recipe_file,char *input_file,char *output_file)
{
  struct recipe *recipe=recipe_read_from_file(recipe_file);
  if (!recipe) return -1;

  unsigned char *buffer;

  int fd=open(input_file,O_RDONLY);
  if (fd==-1) {
    snprintf(recipe_error,1024,"Could not open recipe file '%s'\n",input_file);
    return -1;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    snprintf(recipe_error,1024,"Could not stat recipe file '%s'\n",input_file);
    close(fd); return -1;
  }

  buffer=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer==MAP_FAILED) {
    snprintf(recipe_error,1024,"Could not memory map recipe file '%s'\n",input_file);
    close(fd); return -1; 
  }

  unsigned char out_buffer[1024];
  int r=recipe_compress(recipe,(char *)buffer,stat.st_size,out_buffer,1024);

  munmap(buffer,stat.st_size); close(fd);

  if (r<0) return -1;
  
  FILE *f=fopen(output_file,"w");
  if (!f) {
    snprintf(recipe_error,1024,"Could not write compressed file '%s'\n",output_file);
    return -1;
  }
  int wrote=fwrite(out_buffer,r,1,f);
  fclose(f);
  if (wrote!=1) {
    snprintf(recipe_error,1024,"Could not write compressed data into '%s'\n",output_file);
    return -1;
  }

  return r;
}

int recipe_main(int argc,char *argv[], stats_handle *h)
{
  if (argc<=2) {
    fprintf(stderr,"'smac recipe' command requires further arguments.\n");
    exit(-1);
  }

  if (!strcasecmp(argv[2],"parse")) {
    if (argc<=3) {
      fprintf(stderr,"'smac recipe parse' requires name of recipe to load.\n");
      exit(-1);
    }
    struct recipe *recipe = recipe_read_from_file(argv[3]);
    if (!recipe) {
      fprintf(stderr,"%s",recipe_error);
      exit(-1);
    } 
    printf("recipe=%p\n",recipe);
    printf("recipe->field_count=%d\n",recipe->field_count);
  } else if (!strcasecmp(argv[2],"compress")) {
    if (argc<=5) {
      fprintf(stderr,"'smac recipe compress' requires recipe, input and output files.\n");
      exit(-1);
    }
    if (recipe_compress_file(argv[3],argv[4],argv[5])==-1) {
      fprintf(stderr,"%s",recipe_error);
      exit(-1);
    }
    else return 0;
  } else {
    fprintf(stderr,"unknown 'smac recipe' sub-command '%s'.\n",argv[2]);
      exit(-1);
  }

  return 0;
}
