#include<stdio.h>
#include<mpi.h>
#include<pthread.h>
#include<stdlib.h>
#include<unistd.h>
#include<assert.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<fcntl.h>
#include<string.h>

enum modes {
    MULTIRATE_MODE_PAIRWISE,
    MULTIRATE_MODE_ALLTOALL
};

enum pairwise_modes {
    MULTIRATE_PAIRWISE_PROCESS_PROCESS,
    MULTIRATE_PAIRWISE_THREAD_THREAD,
    MULTIRATE_PAIRWISE_PROCESS_THREAD,
    MULTIRATE_PAIRWISE_THREAD_PROCESS
};

#define MULTIRATE_SYNC_TAG  1337
#define MULTIRATE_RTS_TAG   1338

int warmup_num = 10;
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

int mode;
int pairwise_mode;

char *mmap_filename = "/scratch/local/arm/mmap_t2p";
int *shm_ready;

static pthread_barrier_t barrier;

void test_init(void);

MPI_Comm *comms;
MPI_Comm sender_comm;
MPI_Comm recver_comm;

int mmap_fd;

typedef struct {
    int id;
    int my_pair;
    MPI_Comm comm;
} thread_info;

void *create_shm(char *mmap_filename, size_t size)
{
    int rc;
    void  *retval = NULL;

    mmap_fd = open(mmap_filename, O_CREAT | O_RDWR, 00666);
    if (mmap_fd < 0) {
        printf("failed to open shm fd.\n");
        abort();
    }

    /* if we can write to this file with size. */
    if (ftruncate(mmap_fd, size) == 0) {
        rc = lseek(mmap_fd, size-1, SEEK_SET);
        if (rc == -1) {
            printf("lseek failed\n");
            goto mmap_failed;
       }

        /* write empty string at EOF */
        rc = write(mmap_fd, "", 1);
        if (rc != 1) {
            printf("write to shm failed./n");
            goto mmap_failed;
        }

        retval = mmap(NULL, size,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    mmap_fd,
                    0);

        if (retval == MAP_FAILED | retval == NULL) {
            printf("mmap failed\n");
            goto mmap_failed;
        }

        return retval;
    }

mmap_failed:
    close(mmap_fd);
    abort();
}

void *connect_shm(char *mmap_filename, size_t size)
{
    int rc;
    void *retval;

    mmap_fd = open(mmap_filename, O_RDWR, 00666);
    if (mmap_fd < 0) {
        printf("failed to open shm fd.\n");
        abort();
    }

    retval = mmap(NULL, size,
                PROT_READ,
                MAP_SHARED,
                mmap_fd,
                0);

    if (retval == MAP_FAILED | retval == NULL) {
        printf("mmap client failed.\n");
        close(mmap_fd);
        abort();
    }

    return retval;
}


void preprocess_args(int argc, char **argv)
{
    /* Copied this part from artem's benchmark. He said usually
     * we can't access argv before we call MPI_Init but by doing this,
     * it works. I dont know anything about this but it does work. */
    int i;
    for(i=0;i<argc;i++){
        if(!strcmp(argv[i], "-Dthrds"))
           want_multithread = 0;
    }
}

void process_args(int argc, char **argv)
{
    int c;
    while((c = getopt(argc,argv, "n:m:s:i:w:D:x:y:cpat:")) != -1){
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
                    pairwise_mode = MULTIRATE_PAIRWISE_PROCESS_PROCESS;
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
            case 'p':
                mode = MULTIRATE_MODE_PAIRWISE;
                break;
            case 'a':
                mode = MULTIRATE_MODE_ALLTOALL;
                break;
            case 't':
                x_send_thread = y_recv_thread = atoi(optarg);
                pairwise_mode = MULTIRATE_PAIRWISE_THREAD_THREAD;
                break;
            default:
                c = -1;
                exit(-1);
        }
    }
}

void process_sync(void)
{
    if (i_am_sender)
        MPI_Barrier(sender_comm);
    else
        MPI_Barrier(recver_comm);
}

int main(int argc,char **argv)
{
        int thread_level;

        /* Process the arguments. */
        preprocess_args(argc, argv);

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

        /** if (me == 0 || me == 1) { */
        /**     shm_ready= (int*) create_shm(mmap_filename, 4); */
        /**     *shm_ready = 0; */
        /**     MPI_Barrier(MPI_COMM_WORLD); */
        /** } else { */
        /**     MPI_Barrier(MPI_COMM_WORLD); */
        /**     shm_ready = (int*) connect_shm(mmap_filename, 4); */
        /** } */
/**  */
        int *sender = (int*)malloc(sizeof(int) * n_send_process);
        int *recver = (int*)malloc(sizeof(int) * m_recv_process);

        for (int i = 0; i < n_send_process;i++) {
            sender[i] = i;
        }

        for (int i = 0; i < m_recv_process;i++) {
            recver[i] = size/2+i;
        }

        MPI_Group sender_group, recver_group, world_group;
        MPI_Comm_group(MPI_COMM_WORLD, &world_group);
        MPI_Group_incl(world_group, n_send_process, sender, &sender_group);
        MPI_Group_incl(world_group, m_recv_process, recver, &recver_group);

        MPI_Comm_create_group(MPI_COMM_WORLD, sender_group, 0, &sender_comm);
        MPI_Comm_create_group(MPI_COMM_WORLD, recver_group, 0, &recver_comm);

        if (me < size/2)
            i_am_sender = 1;
        else
            i_am_sender = 0;

        if ((i_am_sender && me < n_send_process) ||
                (!(i_am_sender) && me-(size/2) < m_recv_process )) {
            i_am_participant = 1;
        }

        test_init();

get_out:
        MPI_Finalize();
		return 0;
}

void *comm_alltoall(void *info)
{
        char *buffer;
        int i,j,k,iteration;
        int offset, comm_offset;
        double start, end;

        buffer = (char*) malloc(msg_size);

        thread_info *t_info = (thread_info*) info;
        int tid = t_info->id;

        pthread_barrier_wait(&barrier);

        if(i_am_sender){

            int local_rank = me;
            int receiver_offset = size/2;
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
                        MPI_Recv(buffer, 1, MPI_BYTE, receiver_offset+i, MULTIRATE_RTS_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        /** printf("%d: recv from %d\n",me, (2*i) +1); */
                }
                pthread_barrier_wait(&barrier);
                MPI_Barrier(sender_comm);

                /* post isend to each reciever thread on each reciever. */
                for(i=0;i<m_recv_process;i++){
                    for(j=0;j<y_recv_thread;j++){
                        offset = (i*y_recv_thread + j) * window_size;
                        comm_offset = ((local_rank*x_send_thread*y_recv_thread) + (tid * y_recv_thread) + j) * separated_comm;

                        for(k=0;k<window_size;k++){
                            MPI_Isend(buffer, msg_size, MPI_BYTE, receiver_offset+i, j, comms[comm_offset], &request[offset+k]);
                        }
                    }
                }
                MPI_Waitall(total_request, request, status);

                if(tid==0){
                    for(i=0;i<m_recv_process;i++){
                        MPI_Recv(buffer, 1, MPI_BYTE, receiver_offset+i, MULTIRATE_SYNC_TAG , MPI_COMM_WORLD, MPI_STATUS_IGNORE);
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
                            MPI_Irecv(buffer, msg_size, MPI_BYTE, i, tid, comms[comm_offset], &request[offset+k]);
                        }
                    }
                }
                // pre-post message
                pthread_barrier_wait(&barrier);
                MPI_Barrier(recver_comm);

                // ping sender now that we are ready;
                if(tid ==0) {
                    MPI_Send(buffer, 1, MPI_BYTE, 0, MULTIRATE_RTS_TAG, MPI_COMM_WORLD);
                }

                MPI_Waitall(total_request, request, MPI_STATUS_IGNORE);

                /* We have to send something back to tell that we are done recving. */
                if(tid ==0){
                    for(i=0;i<n_send_process;i++){
                        /** printf("%d: posting fin to %d\n",me,(2*i)); */
                        MPI_Send(buffer, 1, MPI_BYTE, i, MULTIRATE_SYNC_TAG, MPI_COMM_WORLD);
                    }
                }
                pthread_barrier_wait(&barrier);
                MPI_Barrier(recver_comm);
            }
        }
}

void *comm_pairwise(void *info)
{
        int i,j,k,iteration;
        int offset, comm_offset;
        int total_request = window_size;
        char *buffer;
        double start, end;

        thread_info *t_info = (thread_info*) info;
        int tid = t_info->id;

        pthread_barrier_wait(&barrier);
        process_sync();

        /* allocate buffer */
        buffer = (char*) malloc(msg_size);

        if(i_am_sender){
            MPI_Request request[ total_request ];
            MPI_Status status [ total_request ];

            for(iteration = 0; iteration < iter_num + warmup_num; iteration++) {

                /* Basically we start taking time after some number of runs. */
                if(iteration == warmup_num){
                    pthread_barrier_wait(&barrier);
                    MPI_Barrier(sender_comm);
                    if(tid==0)
                        g_start = MPI_Wtime();
                }

                MPI_Recv(buffer, 1, MPI_BYTE, t_info->my_pair, MULTIRATE_RTS_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                /* post isend to each reciever thread on each reciever. */
                for(k=0;k<window_size;k++){
                    MPI_Isend(buffer, msg_size, MPI_BYTE, t_info->my_pair, k, t_info->comm, &request[k]);
                }
                MPI_Waitall(total_request, request, status);

                /* wait for the receiver to respond */
                MPI_Recv(buffer, 1, MPI_BYTE, t_info->my_pair, MULTIRATE_SYNC_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }

        } else {

            MPI_Request request[ total_request ];
            MPI_Status status [ total_request ];

            for(iteration = 0; iteration < iter_num + warmup_num; iteration++) {
                /* Basically we start taking time after some number of runs. */
                if(iteration == warmup_num){
                    pthread_barrier_wait(&barrier);
                    MPI_Barrier(recver_comm);
                }

                /* post irecv to each reciever thread on each reciever. */
                for(k=0;k<window_size;k++){
                    MPI_Irecv(buffer, msg_size, MPI_BYTE, t_info->my_pair, k, t_info->comm, &request[k]);
                }

                /* ping sender now that we are ready. */
                MPI_Send(buffer, 1, MPI_BYTE, t_info->my_pair, MULTIRATE_RTS_TAG, MPI_COMM_WORLD);
                MPI_Waitall(total_request, request, status);

                /* let the sender know we finished. */
                MPI_Send(buffer, 1, MPI_BYTE, t_info->my_pair, MULTIRATE_SYNC_TAG, MPI_COMM_WORLD);
            }
        }
}

void test_init(void)
{
        int i;
        int num_threads;
        double end;
        pthread_t *id;
        thread_info *t_info;

        if (mode == MULTIRATE_MODE_ALLTOALL)
            num_comm = n_send_process * x_send_thread * m_recv_process * y_recv_thread;
        else {
            if (pairwise_mode == MULTIRATE_PAIRWISE_THREAD_THREAD)
                num_comm = x_send_thread;
            else
                num_comm = n_send_process;
        }

        /* Spawn the threads */
        if(i_am_sender)
            num_threads = x_send_thread;
        else
            num_threads = y_recv_thread;

        id =     (pthread_t*)   malloc(sizeof(pthread_t)   * num_threads);
        comms =   (MPI_Comm*)    malloc(sizeof(MPI_Comm)    * num_comm);
        t_info = (thread_info*) malloc(sizeof(thread_info) * num_threads);

        pthread_barrier_init(&barrier, NULL, num_threads);

        /* Duplicate communicator */
        for(i=0;i<num_comm;i++){
            MPI_Comm_dup(MPI_COMM_WORLD, &comms[i]);
        }

        /* Sync */
        MPI_Barrier(MPI_COMM_WORLD);

        /* Work if the process is the part of this run. */
        if (i_am_participant) {
            for(i=0;i<num_threads;i++) {
                t_info[i].id = i;

                switch (mode) {
                case MULTIRATE_MODE_ALLTOALL:
                    pthread_create(&id[i], NULL, comm_alltoall, (void*) &t_info[i]);
                    break;
                case MULTIRATE_MODE_PAIRWISE:
                    if (pairwise_mode == MULTIRATE_PAIRWISE_THREAD_THREAD) {
                        t_info[i].my_pair = (me+1)%2;
                        t_info[i].comm = MPI_COMM_WORLD;
                        if (separated_comm)
                            t_info[i].comm = comms[i];
                    } else if (pairwise_mode == MULTIRATE_PAIRWISE_PROCESS_PROCESS) {
                        /* MULTIRATE_PAIRWISE_PROCESS */
                        t_info[i].comm = comms[i];
                        if (i_am_sender)
                            t_info[i].my_pair = (size/2+me);
                        else
                            t_info[i].my_pair = (me-size/2);
                    }

                    pthread_create(&id[i], NULL, comm_pairwise, (void*) &t_info[i]);
                    break;
                default:
                    /* error handling? */
                    break;
                }
            }

            /* join & sync */
            for(i=0;i<num_threads;i++){
                pthread_join(id[i], NULL);
            }

            process_sync();
            end = MPI_Wtime();

            /* write to shm if you are node leader. */
            /** if (me == 0 || me == 1) */
            /**     *shm_ready = 1; */

        } else {
            /* not participant process. Wait for shm ready. */
            /** while (*shm_ready == 0) { */
            /**     sleep(3); */
            /**     sched_yield(); */
            /** } */
        }
        MPI_Barrier(MPI_COMM_WORLD);

        double elapsed_time = end - g_start;
        double msg_rate = (double)(num_comm * window_size * iter_num)/elapsed_time;
        double bandwidth = msg_rate * msg_size * 8 / 1000000000;
        double latency = (elapsed_time * 1000000)/(num_comm * window_size * iter_num);


        /* output message rate */
        if(me == 0) {
            printf("%d\t%d\t%d\t%d\t%d\t%d\t%.2lf Gbps\t%.2lf usec\t%.2lf msg/s\n",
                                              n_send_process, x_send_thread,
                                              m_recv_process, y_recv_thread,
                                              msg_size, window_size,
                                              bandwidth,
                                              latency,
                                              msg_rate);

        }
        pthread_barrier_destroy(&barrier);
}
