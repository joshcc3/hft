from bcc import BPF #1
from bcc.utils import printb

device = "lo" #2
#b = BPF(src_file="mytest.c") #3
#fn = b.load_func("mytest", BPF.XDP) #4
b = BPF(src_file="lll_1_test.bpf.c") #3
fn = b.load_func("lll_1_test", BPF.XDP) #4
b.attach_xdp(device, fn, 0) #5

for table_name in b.tables:
    print("Table:", table_name)
#try:
#    b.trace_print() #6
#except KeyboardInterrupt: #7

#    dist = b.get_table("xsks_map") #8
#    for k, v in sorted(dist.items()): #9
#        print("DEST_PORT : %10d, COUNT : %10d" % (k.value, v.value)) #10

#b.remove_xdp(device, 0) #11
