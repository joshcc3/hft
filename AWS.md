sudo yum install git net-tools bcc libbpf-devel libxdp-devel 
sudo yum group install 'Development Tools'
sudo yum install emacs clang
sudo yum install tmux
sudo yum install llvm
sudo yum install tcpdump
sudo yum install cmake
ssh-keygen 
cat ~/.ssh/id_rsa.pub 
git clone git@github.com:joshcc3/hft.git



sudo yum install bpftool
sudo amazon-linux-extras install epel

git clone https://github.com/the-tcpdump-group/libpcap.git
cmake .
make
sudo cp libpcap.so /usr/lib64/libpcap.so.1.10.1
sudo ln -s libpcap.so.1.10.1 libpcap.so.1
sudo cp -r pcap /usr/include/pcap
sudo ldconfig

git clone https://github.com/xdp-project/xdp-tools.git
cd xdp-tools/
./configure 
make
cp -r lib/libbpf/src/libbpf.so /usr/lib64/libbpf.so.1.2.2
sudo cp lib/libbpf/src/libbpf.so /usr/lib64/libbpf.so.1.2.2
sudo cp "lib/libxdp/libxdp.so*" /usr/lib64/
sudo cp "lib/libxdp/libxdp.so" /usr/lib64/
sudo cp "lib/libxdp/libxdp.so.1" /usr/lib64/
sudo cp "lib/libxdp/libxdp.so.1.4.0" /usr/lib64/
sudo cp lib/libbpf/src/libbpf.so.1.2.0 /usr/lib64/libbpf.so.1.2.0

sudo ln -s libxdp.so.1  libxdp.so.1.4.0 
sudo ln -s libxdp.so.1.4.0 libxdp.so.1  
sudo ldconfig

sudo cp  headers/bpf/* /usr/include/bpf/
cp -r /usr/include/linux ../linux-headers.bk
sudo cp headers/linux/* /usr/include/linux/x
sudo cp -r headers/xdp /usr/include/xdp

cat /proc/sys/vm/nr_hugepages
sudo sysctl -w vm.nr_hugepages=128
cat /proc/sys/vm/nr_hugepages
cat /proc/meminfo 

clang++ -g -std=c++17  main.cpp -lxdp -lbpf -Wc++17-extensions -o main 
