mpirun --allow-run-as-root -np 10 ./build/master_slave_v1 run \
    ../../../data/100.10.1.txt \
    --diversity-weight-edge 0.9 \
    --diversity-weight-assignment 0.1