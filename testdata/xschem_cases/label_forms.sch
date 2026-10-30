v {xschem version=3.4.5 file_version=1.2}
G {}
K {}
V {}
S {}
E {}
N 0 0 200 0 {lab=A[1:0]}
N 0 100 200 100 {lab=2*B[1:0]}
N 0 200 200 200 {lab=C[0,4,2:1]}
N 0 300 200 300 {}
N 0 400 200 400 {lab="expr(1+2)"}
C {lab_pin.sym} 0 0 0 0 {name=p1 lab=A[1:0]}
C {lab_pin.sym} 0 100 0 0 {name=p2 lab=2*B[1:0]}
C {lab_pin.sym} 0 200 0 0 {name=p3 lab=C[0,4,2:1]}
C {lab_pin.sym} 200 200 0 1 {name=p4 lab=C[1:0]*2}
C {lab_pin.sym} 0 300 0 0 {name=p5}
C {lab_pin.sym} 0 400 0 0 {name=p6 lab="expr(1+2)"}
