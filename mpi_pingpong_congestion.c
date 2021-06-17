/*******************************************************************************************************************
      Congestion benchmark based on Abhinav Bhatele's WICON benchmark
      Performs ping pong communication between different nodes paired with a random_value mapping. PPN is set to 1 for communication.
      A second process per node performs busy waiting.
      ITER=number of iterations
      SIZE=how many different message sizes to test
      MIN_MSG_SIZE in bytes
      DISTANCE=if set, reports node distance
      Other specifications: Blocking, Warmup of 10 iterations, Cleans cache before every communication phase, Excludes 25% of max values as outliers 
      How to execute: allocate N nodes, 2*N processes, placement=ROUND_ROBIN
*******************************************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <sys/time.h>
#include <math.h>
#ifdef DISTANCE
#include "distance.h"
#endif

#define ITER 400
#define MIN_MSG_SIZE 1
#define MAX_MSG_SIZE 4096*1024
#define SIZE 25
#define SHUFFLE_ITERATIONS 2

#define LLC_SIZE 16*1024*1024
int max(int a, int b) {
	return a>b?a:b;
}

void sort(double * res,int length) {
	int i,j;
	double temp;
	for (i=1;i<length;i++)
		for (j=length-1;j>=i;j--)
			if (res[j-1]>res[j]) {
				temp=res[j];	
				res[j]=res[j-1];
				res[j-1]=temp;
			}

}

double random_value(double * res, int length) {
	srand(time(0));
	int i = rand() % ITER;
	return res[i];
}
/* Creating a random_value map 
 * Code taken from Matt Reilly's benchmark:
 * http://www.bigncomputing.org/Big_N_Computing/Big_N_Computing/Entries/2008/4/14_High_Processor_Count_Computing.html
 */

static int random_value_ready = 0; 
void init_random_value()
{
	srand48(33550336); 
	random_value_ready = 1; 
}
  
double get_random_value_double()
{
	if(random_value_ready == 0) init_random_value();
	return drand48(); 
}

int get_random_value_int(int max)
{
	double dr;
	int res = max;
	while (res >= max) {
		dr = get_random_value_double() * ((double) max);
		res = (int) floor(dr); 
  	}
  	return res; 
}

void build_random_value_map(int init, int map_size, int size,int * map)
{

	int i, j, k, p, q;
  
	if(map_size & 1) {
		fprintf(stderr, "Random maps must be even length\n"); 
		exit(-1); 
	}
  
	if(init) {
    		// build an initial map that maps all entries K to K + (K mod 2)
		for(i = 0; i < map_size; i++) 
			map[i] = i ^ 1; 
   
  	}

	// Now do random_value pair swaps
  	for(j = 0; j < SHUFFLE_ITERATIONS; j++) {
    		for(i = 0; i < map_size; i++) {
      			// for each mapping entry, pick someone to swap with.
      			k = i;
	      		while ((k == i) || (k == map[i])) {
				k = get_random_value_int(map_size);
      			}
			p = map[i];
			q = map[k];
			map[i] = q;
			map[p] = k;
			map[k] = p;
			map[q] = i; 
    	}
  	}
}

int main(int argc, char * argv[]) {

	int size,rank;
	int i,j,k;

	MPI_Init(&argc,&argv);

	MPI_Comm_size(MPI_COMM_WORLD,&size);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	if (argc < 3) {
		if (rank == 0) {
			printf("Usage: ./exec num_nodes num_messages\n");
		}
		exit(0);
	}
	int Nodes=atoi(argv[1]);
	int Messages=atoi(argv[2]);
	int * pe=malloc(Messages*sizeof(int));

	#ifdef SWEEP
	char * buffer = malloc(LLC_SIZE);
	#endif

	/**** Create random_value communication map ****/	
	int * map = (int *) malloc(sizeof(int) * size);
	int trial;
	for (i=0;i<Messages;i++) {
		if(rank == 0) 
			build_random_value_map(1, size, size,map); 
		
		
	      	// Broadcast the routing map.
		MPI_Bcast(map, size, MPI_INT, 0, MPI_COMM_WORLD); 
		pe[i] = map[rank]; 
	}

	double * res=malloc(ITER*sizeof(double*));
	struct timeval ts,tf;
	int * msg_size=malloc(SIZE*sizeof(int));
	msg_size[0]=MIN_MSG_SIZE;
	for (i=1;i<SIZE;i++)
		msg_size[i]=msg_size[i-1]*2;

	unsigned char * ping,* pong;
	double maxval[SIZE];
	double a=0;

	MPI_Status * status_ping=malloc(Messages*sizeof(MPI_Status));
	MPI_Status * status_pong=malloc(Messages*sizeof(MPI_Status));	
	MPI_Request * request_ping=malloc(Messages*sizeof(MPI_Request));
	MPI_Request * request_pong=malloc(Messages*sizeof(MPI_Request));

   	for (k=0;k<SIZE;k++) {
		ping=malloc(msg_size[k]*sizeof(unsigned char));
		pong=malloc(msg_size[k]*sizeof(unsigned char));
		
   		for (j=0;j<msg_size[k];j++) {
   			ping[j]=(unsigned char)(i+1)*j;   
			pong[j]=(unsigned char)(i+1)*j;   
		}
		
		MPI_Barrier(MPI_COMM_WORLD);

		// Warmup
		for (i=0;i<10;i++) {
			for (j=0;j<Messages;j++) {
       		    	MPI_Isend(&ping[0],msg_size[k],MPI_UNSIGNED_CHAR,pe[j],50+rank+j,MPI_COMM_WORLD,&request_ping[j]);
	 	       		MPI_Irecv(&pong[0],msg_size[k],MPI_UNSIGNED_CHAR,pe[j],50+j+pe[j],MPI_COMM_WORLD,&request_pong[j]);
			}
			MPI_Waitall(Messages,request_ping,status_ping);
			MPI_Waitall(Messages,request_pong,status_pong);			
		}
		//End of warmup
		MPI_Barrier(MPI_COMM_WORLD);		
	
		//Benchmark
	
		for (i=0;i<ITER;i++) {
			#ifdef SWEEP
			memset(buffer, i%26+26, LLC_SIZE);
			#endif
			gettimeofday(&ts,NULL);
			for (j=0;j<Messages;j++) {
     		    MPI_Isend(&ping[0],msg_size[k],MPI_UNSIGNED_CHAR,pe[j],50+rank+j,MPI_COMM_WORLD,&request_ping[j]);
	 	       	MPI_Irecv(&pong[0],msg_size[k],MPI_UNSIGNED_CHAR,pe[j],50+pe[j]+j,MPI_COMM_WORLD,&request_pong[j]);
			}
			MPI_Waitall(Messages,request_ping,status_pong);
			MPI_Waitall(Messages,request_pong,status_ping);
			gettimeofday(&tf,NULL);
	        res[i]=(tf.tv_sec-ts.tv_sec)+(tf.tv_usec-ts.tv_usec)*0.000001;                
		}
	    	
		free(ping);
		free(pong);

		sort(res,ITER);
	   	maxval[k]=res[ITER-(int)(0.25*ITER)];
        MPI_Barrier(MPI_COMM_WORLD);
   	}
   	free(res);    

#ifdef DISTANCE
	char *s1, *s2;
	s1=malloc(MPI_MAX_PROCESSOR_NAME*sizeof(char));
	s2=malloc(MPI_MAX_PROCESSOR_NAME*sizeof(char));

	int length;
	int * dist=malloc(Messages*sizeof(int));
	for (j=0;j<Messages;j++) {
		if (rank<pe[j]) {
			MPI_Get_processor_name(s1,&length);
			MPI_Send(s1,length,MPI_CHAR,pe[j],55,MPI_COMM_WORLD);	
			MPI_Recv(&dist[j],1,MPI_INT,pe[j],56,MPI_COMM_WORLD,&status_ping[j]);
		}
		else if (rank>=pe[j]) {
			MPI_Recv(s2,MPI_MAX_PROCESSOR_NAME,MPI_CHAR,pe[j],55,MPI_COMM_WORLD,&status_ping[j]);		
			MPI_Get_processor_name(s1,&length);
			int d1=get_id(s1);
			int d2=get_id(s2);
			dist[j]=abs_distance(d1,d2);
			MPI_Send(&dist[j],1,MPI_INT,pe[j],56,MPI_COMM_WORLD);
		}
	}
	free(s1);
	free(s2);

#endif

	double *max_times;
	if (rank==0) 
	        max_times=malloc(SIZE*sizeof(double));
	for (k=0;k<SIZE;k++) 
		MPI_Reduce(&maxval[k],&max_times[k],1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
#ifdef DISTANCE

	int max_dist=0,avg_dist=0,temp;
	for (j=0;j<Messages;j++) {
		MPI_Reduce(&dist[j],&temp,1,MPI_INT,MPI_MAX,0,MPI_COMM_WORLD);
		max_dist=max(temp,max_dist);
		MPI_Reduce(&dist[j],&temp,1,MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);
		avg_dist+=temp;
	}
	avg_dist/=(Messages*Nodes);
#endif
	if (rank==0) {
		char * s=malloc(40*sizeof(char));
		#ifdef SWEEP
		sprintf(s,"res_sweep_Benchmark_PPN_%d_nodes_%d_#_%d",size/Nodes,Nodes,Messages);
		#else
		sprintf(s,"res_Benchmark_PPN_%d_nodes_%d_#_%d",size/Nodes,Nodes,Messages);

		#endif
		FILE * f=fopen(s,"a");
		fprintf(f,"PPN N S time\n");
		for (i=0;i<SIZE;i++)  {
			fprintf(f,"%d %d %d %lf\n",size/Nodes,Nodes,msg_size[i],max_times[i]);
			fprintf(stderr,"%d %d %d %lf\n",size/Nodes,Nodes,msg_size[i],max_times[i]);
		}
		fclose(f);
	} 
	printf("%lf ",a);   
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
	return 0;
}
