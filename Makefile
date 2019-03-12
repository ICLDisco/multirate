#CFLAGS = -O3 -pthread -I/spack/opt/spack/linux-rhel7-x86_64/gcc-4.8.5/intel-parallel-studio-cluster.2017.4-yzbdvig6mk4wizd5zcolj5foz2q4wwcx/compilers_and_libraries_2017.4.196/linux/mpi/intel64/include -L/spack/opt/spack/linux-rhel7-x86_64/gcc-4.8.5/intel-parallel-studio-cluster.2017.4-yzbdvig6mk4wizd5zcolj5foz2q4wwcx/compilers_and_libraries_2017.4.196/linux/mpi/intel64/lib

CFLAGS = -pthread
#PREP =/home/tpatinya/opt/otf2/bin/scorep
CC = ${PREP} mpicc

multirate: multirate.c
	$(CC) $(CFLAGS) -o multirate multirate.c

all: a2a pair rma rput

clean:
	rm -f multirate
