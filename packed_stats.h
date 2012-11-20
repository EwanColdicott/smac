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

typedef struct node {
  long long count;
  unsigned int counts[69];
  struct node *children[69];
} node;

typedef struct compressed_stats_handle {
  FILE *file;
  unsigned char *mmap;
  int fileLength;
  int dummyOffset;
  unsigned char *buffer;
  unsigned char *bufferBitmap;  

  unsigned int rootNodeAddress;
  unsigned int totalCount;
} stats_handle;

struct node *extractNode(char *string,int len,stats_handle *h);
struct node *extractNodeAt(char *s,unsigned int nodeAddress,int count,stats_handle *h);
int extractVector(char *string,int len,stats_handle *h,unsigned int v[69]);
int vectorReport(char *name,unsigned int v[69],int s);
int dumpNode(struct node *n);

void stats_handle_free(stats_handle *h);
stats_handle *stats_new_handle(char *file);
unsigned char *getCompressedBytes(stats_handle *h,int start,int count);