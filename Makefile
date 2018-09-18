#CFLAGS = -O3 -pthread -I/spack/opt/spack/linux-rhel7-x86_64/gcc-4.8.5/intel-parallel-studio-cluster.2017.4-yzbdvig6mk4wizd5zcolj5foz2q4wwcx/compilers_and_libraries_2017.4.196/linux/mpi/intel64/include -L/spack/opt/spack/linux-rhel7-x86_64/gcc-4.8.5/intel-parallel-studio-cluster.2017.4-yzbdvig6mk4wizd5zcolj5foz2q4wwcx/compilers_and_libraries_2017.4.196/linux/mpi/intel64/lib

CFLAGS = -g -pthread
#PREP =/home/tpatinya/opt/otf2/bin/scorep
MPICC = ${PREP} mpicc


pair: pairwise.c
	$(MPICC) $(CFLAGS) -o pairwise pairwise.c 

pair_no_switch: pairwise_no_switching.c
	$(MPICC) $(CFLAGS) -o pairwise_no_switching pairwise_no_switching.c 

a2a: msgrate_process.c
	$(MPICC) $(CFLAGS) -o msgrate_process msgrate_process.c

rma: pairwise_rma.c
	$(MPICC) $(CFLAGS) -o pairwise_rma pairwise_rma.c

rput: pairwise_rput.c
	$(MPICC) $(CFLAGS) -o pairwise_rput pairwise_rput.c

all: a2a pair rma rput

clean:
	rm -f msgrate_process pairwise pairwise_rma pairwise_rput
