# Multirate benchmark

This is multithreaded MPI communication benchmark with multiple flavors.

To make, just run `make` with the provided makefile.

Benchmark has two modes: Pairwise and Alltoall.

Usage: `mpirun ./multirate (options)`

Available options:
```
Communication Pattern (pick one):
-p : Operate in Pairwise mode. (default)
-a : Operate in Alltoall mode.

Alltoall mode options:
-n (k) : number of sender processes
-m (k) : number of receiver processes
-x (k) : number of sender threads
-y (k) : number of receiver processes

Workload Adjustment:
-t : num_thread_pair (pairwise only) 
-s : message size
-w : window size.
-i : number of iteration

Additional test:
-c : use separated communicator for each pair.
-o : ignore MPI message ordering (allow_overtaking)

```

## General Idea
- Benchmark expected block process placement for process/hybrid mode measurements. ie. node0=rank(0,1,2,3):node1=rank(4,5,6,7)
- Benchmark expected 1 process per node for thread mode measurement.
- Benchmark does a set of warmup before taking measurements.
- Benchmark always pre-posted receive.

