SERVER_IP="127.0.0.1"
SERVER_PORT="8889"
CHILD_COUNT=5000

for i in $(seq 1 $CHILD_COUNT); do
    (
        # Using nc (netcat) to connect to the server
        
	#echo "Hello from connection $i" | nc $SERVER_IP $SERVER_PORT >> /tmp/output.txt
	echo "Hello from connection $i" | nc $SERVER_IP $SERVER_PORT >> /tmp/output.txt

        # Optionally, sleep for a small duration to keep the connection alive
        # sleep 1
    ) &
done
