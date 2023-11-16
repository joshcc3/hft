ebpf tutorial

https://www.kernel.org/doc/html/latest/bpf/linux-notes.html
https://man7.org/linux/man-pages/man7/bpf-helpers.7.html

https://play.instruqt.com/embed/isovalent/tracks/tutorial-getting-started-with-ebpf/challenges/hello-world-maps/assignment#tab-0

https://blog.cloudflare.com/a-story-about-af-xdp-network-namespaces-and-a-cookie/
https://www.mankier.com/3/libxdp 

https://github.com/xdp-project/bpf-examples

bpftool prog list
bpftool prog dump xlated name hello
bpftool prog show name hello
bpftool map show id 11
bpftool map dump id 11
bpftool map lookup id 11 100 0 0 0 0 0 0 0     <-- # each of the bytes
bpftool map update id 11 key 1 0 0 0 0 0 0 0 value 1 0 0 0 0 0 0 0

bpftool prog load hello.bpf.o /sys/fs/bpf/hello

bpftool net attach xdp name hello dev lo
bpftool net detach xdp name hello dev lo
bpftool net list
bpftool prog show name hello
bpftool prog trace log
