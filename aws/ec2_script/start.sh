#!/bin/bash

source ./config.sh
LAB=$1
#Make sure we can find EC2 API tool 
if [ ! -n "$EC2_HOME" ]; then 
    echo "Please set the location of your EC2 API tool into variable EC2_HOME"
    exit
fi

#Setup VPC
if [ ! -n "$EC2_PRIVATE_KEY" ]; then
    echo Please set the location of your EC2 private key into variable EC2_PRIVATE_KEY before continue
    exit 1
fi

if [ ! -n "$EC2_CERT" ]; then
    echo Please set the location of your EC2 certificate into variable EC2_CERT before continue
    exit 1
fi

#Check the file actually exists
if [ ! -f $EC2_PRIVATE_KEY ]; then
	echo "Please put your private key under $DIR/credential/. This is the private key you download with the X.509 certificate (pk-XXXX.pem)"
	exit
fi

if [ ! -f $EC2_CERT ]; then
	echo "Please put your X.509 under $DIR/credential/. (cert-XXX.pem)"
	exit
fi

if [ ! -f $EC2_KEYPAIR_LOCATION ]; then
	echo "Please put the private key of your key pairs under $DIR/credential/."
	exit
fi

if [ $EC2_KEYPAIR_NAME == "" ]; then
	echo "Please put the name of your key pair into the EC2_KEYPAIR_NAME variable. It is needed for the script to launch an instance for you."
	exit
fi

#Get Network Interfaces IDs
echo "Checking network interface settings ... "
output=`$EC2_HOME/bin/ec2-describe-network-interfaces | grep NETWORKINTERFACE -A 2 | grep $MAMG_IP -A 2`
INTF1_ID=`echo $output | grep NETWORKINTERFACE | awk '{print $2}'`
if [ -z "$INTF1_ID" ]; then 
    echo "Interface for management is not there, your environment is not ready, run ./ec2_setup.sh before running this script."
	exit 1
fi

output=`$EC2_HOME/bin/ec2-describe-network-interfaces | grep NETWORKINTERFACE -A 2 | grep $SECOND_IP -A 2`
INTF2_ID=`echo $output | grep NETWORKINTERFACE | awk '{print $2}'`
if [ -z "$INTF2_ID" ]; then 
    echo "Interface for routing/nat is not there, your environment is not ready, run ./ec2_setup.sh before running this script."
	exit 1
fi
echo "passed"
#Allocate Elastic IPs
echo "Checking elastic IPs ..."
output=`$EC2_HOME/bin/ec2-describe-addresses | awk '{print $2}'`
IP_NUM=`echo $output | wc -w`
i=1
while [ $i -le $IP_NUM ]; do
	if [ $i == 1 ]; then
		EIP1=`echo $output | awk '{print $1}'`	
	fi	
	if [ $i == 2 ]; then
		EIP2=`echo $output | awk '{print $2}'`
	fi	
	if [ $i == 3 ]; then
		EIP3=`echo $output | awk '{print $3}'`
	fi
	if [ $i == 4 ]; then
		EIP4=`echo $output | awk '{print $4}'`
	fi
	if [ $i == 5 ]; then
		EIP5=`echo $output | awk '{print $5}'`
	fi
	(( i++ ))
done
if [ ! -n "$EIP1" ]; then
    EIP1=`$EC2_HOME/bin/ec2-allocate-address -d 'vpc' | awk '{print $2}'`
fi
if [ ! -n "$EIP2" ]; then
    EIP2=`$EC2_HOME/bin/ec2-allocate-address -d 'vpc' | awk '{print $2}'`
fi
if [ ! -n "$EIP3" ]; then
    EIP3=`$EC2_HOME/bin/ec2-allocate-address -d 'vpc' | awk '{print $2}'`
fi
if [ ! -n "$EIP4" ]; then
    EIP4=`$EC2_HOME/bin/ec2-allocate-address -d 'vpc' | awk '{print $2}'`
fi
if [ ! -n "$EIP5" ]; then
    EIP5=`$EC2_HOME/bin/ec2-allocate-address -d 'vpc' | awk '{print $2}'`
fi
echo Elastic IPs are $EIP1, $EIP2, $EIP3, $EIP4, $EIP5

#Associate Elastic IPs to Private IPs
echo Associate elastic IPs to private IPs
output=`$EC2_HOME/bin/ec2-describe-addresses | awk '{print $4}'`
EIP1_ID=`echo $output | awk '{print $1}'`
EIP2_ID=`echo $output | awk '{print $2}'`
EIP3_ID=`echo $output | awk '{print $3}'`
EIP4_ID=`echo $output | awk '{print $4}'`
EIP5_ID=`echo $output | awk '{print $5}'`
$EC2_HOME/bin/ec2-associate-address -a $EIP1_ID -p $MAMG_IP -n $INTF1_ID
$EC2_HOME/bin/ec2-associate-address -a $EIP2_ID -p $SECOND_IP -n $INTF2_ID
$EC2_HOME/bin/ec2-associate-address -a $EIP3_ID -p 10.0.1.12 -n $INTF2_ID
$EC2_HOME/bin/ec2-associate-address -a $EIP4_ID -p 10.0.1.13 -n $INTF2_ID
$EC2_HOME/bin/ec2-associate-address -a $EIP5_ID -p 10.0.1.14 -n $INTF2_ID

MAIN_IP=`$EC2_HOME/bin/ec2-describe-addresses | grep $MAMG_IP | awk '{ print $2}'`
SERVER1_IP=`$EC2_HOME/bin/ec2-describe-addresses | grep 10.0.1.10 | awk '{ print $2}'`
SERVER2_IP=`$EC2_HOME/bin/ec2-describe-addresses | grep 10.0.1.12 | awk '{ print $2}'`
SWETH1_IP=`$EC2_HOME/bin/ec2-describe-addresses | grep 10.0.1.13 | awk '{ print $2}'`
SWETH2_IP=`$EC2_HOME/bin/ec2-describe-addresses | grep 10.0.1.14 | awk '{ print $2}'`



if [ "$LAB" != "lab5" ]; then
	echo "Generate IP configuration file for lab 3"
	echo "server1 $SERVER1_IP" > $IPTABLE_FILE
	echo "server2 $SERVER2_IP" >> $IPTABLE_FILE
	echo "sw0-eth1 $SWETH1_IP" >> $IPTABLE_FILE
	echo "sw0-eth2 $SWETH2_IP" >> $IPTABLE_FILE
	echo "sw0-eth3 10.0.1.11" >> $IPTABLE_FILE

	echo "Generate routing table for lab 3"
	echo "0.0.0.0    10.0.1.1 0.0.0.0 eth3" > $RTABLE_FILE
	echo "$SERVER1_IP   $SERVER1_IP 255.255.255.255  eth1" >> $RTABLE_FILE
	echo "$SERVER2_IP   $SERVER2_IP 255.255.255.255  eth2" >> $RTABLE_FILE
else
	echo "Generate IP configuration file for lab 5"
	echo "server1 $SERVER1_IP" > $IPTABLE_FILE
	echo "server2 $SERVER2_IP" >> $IPTABLE_FILE
	echo "sw0-eth1 $SWETH1_IP" >> $IPTABLE_FILE
	echo "sw0-eth2 $SWETH2_IP" >> $IPTABLE_FILE

	echo "Generate routing table for lab 5"
	echo "0.0.0.0    10.0.1.1 0.0.0.0 eth1" > $RTABLE_FILE
	echo "$SERVER1_IP   $SERVER1_IP 255.255.255.255  eth2" >> $RTABLE_FILE
	echo "$SERVER2_IP   $SERVER2_IP 255.255.255.255  eth2" >> $RTABLE_FILE
fi
#Update ssh 

#Launch EC2 Instance
RUNNING_INSTANCE=`$EC2_HOME/bin/ec2-describe-instances | grep $AMI_ID | grep running | awk '{print $2}'`
echo $RUNNING_INSTANCE
if [ ! -n "$RUNNING_INSTANCE" ]; then
    echo There is no running instance, check if there is stopped instance
    STOPPED_INSTANCE=`$EC2_HOME/bin/ec2-describe-instances | grep $AMI_ID | grep stopped | awk '{print $2}'`
    if [ ! -n "$STOPPED_INSTANCE" ]; then
        echo There is no stopped instance, creating one
        output=`$EC2_HOME/bin/ec2-run-instances $AMI_ID -t c1.medium -k $EC2_KEYPAIR_NAME -a $INTF1_ID:0 -a $INTF2_ID:1 -n 1`
        INSTANCE_ID=`echo $output | awk '{print $5}'`
    else
        echo Starting stopped instance
        INSTANCE_ID=`echo $STOPPED_INSTANCE`
        output=`$EC2_HOME/bin/ec2-start-instances $INSTANCE_ID`
    fi
else
    echo "There is one instance running already ..."
    INSTANCE_ID=`echo $RUNNING_INSTANCE`
fi


echo "Your EC2 Instance $INSTANCE_ID started now, generating $SSH_FILE ... "

echo "#!/bin/bash" > $SSH_FILE
echo "echo \"Testing connection...\"" >> $SSH_FILE
echo "ping -c 1 $MAIN_IP" >> $SSH_FILE
echo "echo \"Update configuration file over into the instance\"" >> $SSH_FILE
if [ "$LAB" != "lab5" ]; then
	echo "scp -i $EC2_KEYPAIR_LOCATION ./IP_CONFIG ubuntu@$MAIN_IP:~/cs144_lab3/" >> $SSH_FILE
	echo "scp -i $EC2_KEYPAIR_LOCATION ./rtable ubuntu@$MAIN_IP:~/cs144_lab3/router/" >> $SSH_FILE
else
	echo "scp -i $EC2_KEYPAIR_LOCATION ./IP_CONFIG ubuntu@$MAIN_IP:~/" >> $SSH_FILE
	echo "scp -i $EC2_KEYPAIR_LOCATION ./rtable ubuntu@$MAIN_IP:~/" >> $SSH_FILE
fi
echo "ssh -Y -i $EC2_KEYPAIR_LOCATION ubuntu@$MAIN_IP" >> $SSH_FILE
chmod +x $SSH_FILE

echo "Now you can use $SSH_FILE to login your EC2 Instance"
