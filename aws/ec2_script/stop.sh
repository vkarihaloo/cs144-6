#!/bin/bash

source ./config.sh

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

#Release Elastic IPs
echo "Checking elastic IPs ..."
output=`$EC2_HOME/bin/ec2-describe-addresses`
IP_NUM=`echo "$output" | awk '{print $2}' |wc -w`
i=1
while [ $i -le $IP_NUM ]; do
	if [ $i == 1 ]; then
		EIP1=`echo "$output" | awk '{print $2}' | tr "\n" "\t" | awk '{print $1}'`
		EIP1_ASSOID=`echo "$output" | awk '{ split($0, a, "eipassoc"); print "eipassoc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $1}'`
		EIP1_ALLOCID=`echo "$output" | awk '{ split($0, a, "eipalloc"); print "eipalloc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $1}'`
		if [ -n "$EIP1" ]; then
			echo "disassociating $EIP1 ..."
			echo `$EC2_HOME/bin/ec2-disassociate-address -a $EIP1_ASSOID`
			echo "releasing $EIP1 ... "
			echo `$EC2_HOME/bin/ec2-release-address $EIP1 -a $EIP1_ALLOCID`
			echo "done"
		fi
	fi	
	if [ $i == 2 ]; then
		EIP2=`echo "$output" | awk '{print $2}' | tr "\n" "\t" | awk '{print $2}'`
		EIP2_ASSOID=`echo "$output" | awk '{ split($0, a, "eipassoc"); print "eipassoc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $2}'`
		EIP2_ALLOCID=`echo "$output" | awk '{ split($0, a, "eipalloc"); print "eipalloc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $2}'`
		if [ -n "$EIP2" ]; then
			echo "disassociating $EIP2 ..."
			echo `$EC2_HOME/bin/ec2-disassociate-address -a $EIP2_ASSOID`
			echo "releasing $EIP2 ... "
			echo `$EC2_HOME/bin/ec2-release-address $EIP2 -a $EIP2_ALLOCID`
			echo "done"
		fi
	fi	
	if [ $i == 3 ]; then
		EIP3=`echo "$output" | awk '{print $2}' | tr "\n" "\t" | awk '{print $3}'`
		EIP3_ASSOID=`echo "$output" | awk '{ split($0, a, "eipassoc"); print "eipassoc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $3}'`
		EIP3_ALLOCID=`echo "$output" | awk '{ split($0, a, "eipalloc"); print "eipalloc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $3}'`
		if [ -n "$EIP3" ]; then
			echo "disassociating $EIP3 ..."
			echo `$EC2_HOME/bin/ec2-disassociate-address -a $EIP3_ASSOID`
			echo "releasing $EIP3 ... "
			echo `$EC2_HOME/bin/ec2-release-address $EIP3 -a $EIP3_ALLOCID`
			echo "done"
		fi
	fi
	if [ $i == 4 ]; then
		EIP4=`echo "$output" | awk '{print $2}' | tr "\n" "\t" | awk '{print $4}'`
		EIP4_ASSOID=`echo "$output" | awk '{ split($0, a, "eipassoc"); print "eipassoc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $4}'`
		EIP4_ALLOCID=`echo "$output" | awk '{ split($0, a, "eipalloc"); print "eipalloc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $4}'`
		if [ -n "$EIP4" ]; then
			echo "disassociating $EIP4 ..."
			echo `$EC2_HOME/bin/ec2-disassociate-address -a $EIP4_ASSOID`
			echo "releasing $EIP4 ... "
			echo `$EC2_HOME/bin/ec2-release-address $EIP4 -a $EIP4_ALLOCID`
			echo "done"
		fi
	fi
	if [ $i == 5 ]; then
		EIP5=`echo "$output" | awk '{print $2}' | tr "\n" "\t" | awk '{print $5}'`
		EIP5_ASSOID=`echo "$output" | awk '{ split($0, a, "eipassoc"); print "eipassoc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $5}'`
		EIP5_ALLOCID=`echo "$output" | awk '{ split($0, a, "eipalloc"); print "eipalloc"a[2]}' | awk '{print$1}' | tr "\n" "\t" | awk '{print $5}'`
		if [ -n "$EIP5" ]; then
			echo "disassociating $EIP5 ..."
			echo `$EC2_HOME/bin/ec2-disassociate-address -a $EIP5_ASSOID`
			echo "releasing $EIP5 ... "
			echo `$EC2_HOME/bin/ec2-release-address $EIP5 -a $EIP5_ALLOCID`
			echo "done"
		fi
	fi
	(( i++ ))
done
echo "All elastic IPs are disassociated and released"


#Launch EC2 Instance
RUNNING_INSTANCE=`$EC2_HOME/bin/ec2-describe-instances | grep $AMI_ID | grep running | awk '{print $2}'`
if [ -z "$RUNNING_INSTANCE" ]; then
    echo "There is no running instance, all intances are stopped"
else
    echo "There is one instance running, stopping it ... "
    INSTANCE_ID=`echo $RUNNING_INSTANCE`
	`$EC2_HOME/bin/ec2-stop-instances $INSTANCE_ID`
fi

echo "remove $IPTABLE_FILE"
`rm $IPTABLE_FILE`

echo "remove $RTABLE_FILE"
`rm $RTABLE_FILE`

echo "remove $SSH_FILE"
`rm $SSH_FILE`


echo "Your EC2 Instance $INSTANCE_ID is stopped now, use ./start.sh to run it again."

