# Multirate benchmark

This is multithreaded MPI communication benchmark with multiple flavors.

## Pairwise
Pairwise benchmark measures multithreaded injection rate between 2 processes. Each process will create number of thread pairs to perform ping-ping with each other and measure agregated injection rate. The MPI binding should allow each thread to use the full CPU core (ie, bind MPI process to the whole node or socket). The window size determined how many message is posted per iteration.

Usage: `mpirun ./pairwise -t (num_thread_pair) -s (msg_size) -w (window_size) -i (iteration)`

## Known problem
- pairwise will sometime segfault with Open MPI. If this happens, just run again. This does not affect the correctness of the benchnmark.

