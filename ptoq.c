#include<stdio.h>
#include<mpi.h>
#include<stdlib.h>
#include<unistd.h>
#include<assert.h>

int warmup_num = 0;
int window_size = 256;
int msg_size = 1024;
int iter_num = 1;
int want_multithread = 1;
int me;
int size;
int n_send_process=1, m_recv_process=1;
int x_send_thread=1,y_recv_thread=1;
int i_am_sender;
int i_am_participant = 0;
int num_comm;
int separated_comm = 0;
char *buffer;
double g_start;

static pthread_barrier_t barrier;

void Test_Singlethreaded(void);
void Test_Multithreaded(void);

MPI_Comm *comm;
MPI_Comm sender_comm;
MPI_Comm recver_comm;

typedef struct{
    int id;
    MPI_Comm comm;
}thread_info;

void preprocess_args(int argc, char **argv){

    /* Copied this part from artem's benchmark. He said usually
     * we can't access argv before we call MPI_Init but by doing this,
     * it works. I dont know anything about this but it does work. */
    int i;
    for(i=0;i<argc;i++){
        if(!strcmp(argv[i], "-Dthrds"))
           want_multithread = 0;
    }

}

void process_args(int argc, char **argv){

    int c;
    while((c = getopt(argc,argv, "n:m:s:i:w:D:x:y:c")) != -1){
        switch (c){
            case 'n':
                n_send_process = atoi(optarg);
                break;
            case 'm':
                m_recv_process = atoi(optarg);
                break;
            case 's':
                msg_size = atoi(optarg);
                break;
            case 'i':
                iter_num = atoi(optarg);
                break;
            case 'w':
                window_size = atoi(optarg);
                break;
            case 'D':
                if(!strcmp("thrds",optarg)){
                    want_multithread = 0;
                }
                break;
            case 'x':
                x_send_thread = atoi(optarg);
                break;
            case 'y':
                y_recv_thread = atoi(optarg);
                break;
            case 'c':
                separated_comm = 1;
                break;
            default:
                c = -1;
                exit(-1);
        }
    }
}

int main(int argc,char **argv){

        /* Process the arguments. */
        preprocess_args(argc, argv);
        int thread_level;

        /* Initialize MPI according to the user's desire.*/
        if(want_multithread){
            MPI_Init_thread(&argc,&argv, MPI_THREAD_MULTIPLE, &thread_level);
            if(thread_level != MPI_THREAD_MULTIPLE){
                printf("MPI_THREAD_MULTIPLE requested but MPI implementation cannot provide.\n");
                MPI_Finalize();
                return 0;
            }

        }
        else {
            MPI_Init(&argc, &argv);
		}

        MPI_Comm_size(MPI_COMM_WORLD, &size);
        MPI_Comm_rank(MPI_COMM_WORLD, &me);

        /* Process the arguments. */
        process_args(argc,argv);

        if (size%2) {
			printf("This benchmark needs multiple of two processes\n");
			goto get_out;
		}

		if (n_send_process > size/2 || m_recv_process > size/2) {
			printf("This benchmark need at least %d ranks to satisfy n and m value\n",size/2);
			goto get_out;
		}

        int *sender = (int*)malloc(sizeof(int)*n_send_process);
        int *recver = (int*)malloc(sizeof(int)*m_recv_process);

        for (int i = 0; i < n_send_process;i++){
            sender[i] = i*2;
        }

        for (int i = 0; i < m_recv_process;i++){
            recver[i] = (i*2)+1;
        }

        MPI_Group sender_group, recver_group, world_group;
        MPI_Comm_group(MPI_COMM_WORLD, &world_group);
        MPI_Group_incl(world_group, n_send_process, sender, &sender_group);
        MPI_Group_incl(world_group, m_recv_process, recver, &recver_group);

        MPI_Comm_create_group(MPI_COMM_WORLD, sender_group, 0, &sender_comm);
        MPI_Comm_create_group(MPI_COMM_WORLD, recver_group, 0, &recver_comm);

        i_am_sender = (me+1)%2;
        if ((i_am_sender && me < 2*n_send_process) ||
                (!(i_am_sender) && me < 2*m_recv_process )) {
            i_am_participant = 1;
        }

        Test_Multithreaded();

get_out:
        MPI_Finalize();
		return 0;
}

void *thread_work(void *info){

        char *buffer;
        int i,j,k,iteration;
        int offset, comm_offset;
        int local_rank = me/2;
        double start, end;

        buffer = (char*) malloc(msg_size);

        thread_info *t_info = (thread_info*) info;
        int tid = t_info->id;
        pthread_barrier_wait(&barrier);

        if(i_am_sender){
        thread_info *t_info = (thread_info*) info;
        int tid = t_info->id;
            int total_request = m_recv_process * y_recv_thread * window_size;
            MPI_Request request[ total_request ];
            MPI_Status status [ total_request ];

            for(iteration = 0; iteration < iter_num + warmup_num; iteration++){

                /* Basically we start taking time after some number of runs. */
                if(iteration == warmup_num){
                    pthread_barrier_wait(&barrier);
                    MPI_Barrier(sender_comm);
                    if(tid==0)
                        g_start = MPI_Wtime();
                }

                for (i = 0; i<m_recv_process;i++) {
                    if(tid ==0 && me == 0)
                        MPI_Recv(buffer, 1, MPI_BYTE, (2*i)+1, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        /** printf("%d: recv from %d\n",me, (2*i) +1); */
                }
                pthread_barrier_wait(&barrier);
                MPI_Barrier(sender_comm);

                /* post isend to each reciever thread on each reciever. */
                for(i=0;i<m_recv_process;i++){
                    for(j=0;j<y_recv_thread;j++){
                        offset = (i*y_recv_thread + j) * window_size;
                        comm_offset = ((local_rank*x_send_thread*y_recv_thread) + (tid * y_recv_thread) + j) * separated_comm;
                        /** printf("%d: posting data isend to %d with tag %d\n",me, (2*i),j); */
                        for(k=0;k<window_size;k++){
                            MPI_Isend(buffer, msg_size, MPI_BYTE, (2*i)+1, j, comm[comm_offset], &request[offset+k]);
                        }
                    }
                }
                MPI_Waitall(total_request, request, status);

                if(tid==0){
                    for(i=0;i<m_recv_process;i++){
                        MPI_Recv(buffer, 1, MPI_BYTE, (2*i)+1, 4 , MPI_COMM_WORLD, &status[0]);
                        /** printf("%d: recving fin from %d\n",me,(2*i)+1); */
                     }
                }
                pthread_barrier_wait(&barrier);
                MPI_Barrier(sender_comm);
            }
        }
        else{

            int total_request = n_send_process * x_send_thread * window_size;
            MPI_Request request[ total_request ];
            MPI_Status status [ total_request ];

            for(iteration = 0; iteration < iter_num + warmup_num; iteration++){
                /* Basically we start taking time after some number of runs. */
                if(iteration == warmup_num){
                    pthread_barrier_wait(&barrier);
                    MPI_Barrier(recver_comm);
                    start = MPI_Wtime();
                }

                /* post irecv to each reciever thread on each reciever. */
                for(i=0;i<n_send_process;i++){
                    for(j=0;j<x_send_thread;j++){
                        offset = (i*x_send_thread + j) * window_size;
                        comm_offset = ((i*x_send_thread*y_recv_thread) + (j * y_recv_thread) + tid ) * separated_comm;
                        /** printf("%d: posting data irecv to %d with tag %d\n",me, (2*i),tid); */
                        for(k=0;k<window_size;k++){
                            MPI_Irecv(buffer, msg_size, MPI_BYTE, (2*i), tid, comm[comm_offset], &request[offset+k]);
                        }
                    }
                }
                // pre-post message
                pthread_barrier_wait(&barrier);
                MPI_Barrier(recver_comm);
                // ping sender now that we are ready;
                if(tid ==0) {
                    MPI_Send(buffer, 1, MPI_BYTE, 0, 3, MPI_COMM_WORLD);
                    /** printf("%d: sent ready to 0\n",me); */
                }
                MPI_Waitall(total_request, request, status);

                /* We have to send something back to tell that we are done recving. */
                if(tid ==0){
                    for(i=0;i<n_send_process;i++){
                        /** printf("%d: posting fin to %d\n",me,(2*i)); */
                        MPI_Send(buffer, 1, MPI_BYTE, (2*i), 4, MPI_COMM_WORLD);
                    }
                }
                pthread_barrier_wait(&barrier);
                MPI_Barrier(recver_comm);
            }
        }

}

void Test_Multithreaded(void){

        pthread_t *id;
        thread_info *t_info;
        int i;
        int num_threads;

        num_comm = n_send_process * x_send_thread * m_recv_process * y_recv_thread;
        /* Spawn the threads */
        if(i_am_sender)
            num_threads = x_send_thread;
        else
            num_threads = y_recv_thread;

        id = (pthread_t*) malloc(sizeof(pthread_t) * num_threads);
        t_info = (thread_info*) malloc (sizeof(thread_info) * num_threads);
        pthread_barrier_init(&barrier, NULL, num_threads);
        comm = (MPI_Comm*) malloc(sizeof(MPI_Comm) * num_comm);

        for(i=0;i<num_comm;i++){
            MPI_Comm_dup(MPI_COMM_WORLD, &comm[i]);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (i_am_participant) {
            for(i=0;i<num_threads;i++){
                t_info[i].id = i;
                t_info[i].comm = MPI_COMM_WORLD;
                pthread_create(&id[i], NULL, thread_work, (void*) &t_info[i]);
            }

            // sync

            for(i=0;i<num_threads;i++){
                pthread_join(id[i], NULL);
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);

        /* output message rate */
        if(me == 0){
            printf("%d\t%d\t%d\t%d\t\t%lf\n",n_send_process, x_send_thread
                                            , m_recv_process, y_recv_thread
                                            ,(double)(num_comm *window_size*iter_num/(MPI_Wtime() - g_start)));
        }
        pthread_barrier_destroy(&barrier);
}
