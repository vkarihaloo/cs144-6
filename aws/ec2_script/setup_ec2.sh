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



echo Checking if an VPC already existed in your account .... 
VPC=`$EC2_HOME/bin/ec2-describe-vpcs`
VPCID=`echo $VPC | awk '{print $2}'`
VPCCIDR=`echo $VPC | awk '{print $4}'`
if [ -n "$VPCID" -a "$VPCCIDR" == $DEFAULT_VPC_CIDR ]; then
    echo Using existing VPC ID: $VPCID, $VPCCIDR
else
    echo Creating new VPC
    VPCID=`$EC2_HOME/bin/ec2-create-vpc $DEFAULT_VPC_CIDR | awk '{print $2}'`
fi

#Setup Internet Gateway
echo Checking if an Internet Gateway aleady existed in your account ... 
IGWID=`$EC2_HOME/bin/ec2-describe-internet-gateways | grep INTERNETGATEWAY | awk '{print $2}'`
echo $IGWID
if [ -n "$IGWID" ]; then 
    echo Using existing Internet Gateway ID: $IGWID
else
    echo Creating new Internet Gateway
    IGWID=`$EC2_HOME/bin/ec2-create-internet-gateway | awk '{print $2}'`
fi
echo Associate the Internet Gateway to our VPC
GW_ATTACHMENT=`$EC2_HOME/bin/ec2-describe-internet-gateways $IGWID | grep $VPCID | cut -f 2`

if [ -n "$GW_ATTACHMENT" ]; then
    echo the Internet Gateway already attach to our VPC
else
    echo attaching the Internet Gateway to VPC
    $EC2_HOME/bin/ec2-attach-internet-gateway $IGWID -c $VPCID
fi

#Find out available zone
ZONE=`$EC2_HOME/bin/ec2-describe-availability-zones  | awk '{print $2}' | tr "\n" "\t" | awk '{print $1}'`
echo "Using zone $ZONE"


#Setup Subnets
output=`$EC2_HOME/bin/ec2-describe-subnets | grep $DEFAULT_SUBNET1`
SUBNET1=`echo $output | awk '{print $5}'`
SUBNET1_ID=`echo $output | awk '{print $2}'`
output=`$EC2_HOME/bin/ec2-describe-subnets | grep $DEFAULT_SUBNET2`
SUBNET2=`echo $output | awk '{print $5}'`
SUBNET2_ID=`echo $output | awk '{print $2}'`
if [ "$SUBNET1" != $DEFAULT_SUBNET1 ]; then
    echo Creating subnet $DEFAULT_SUBNET1
    output=`$EC2_HOME/bin/ec2-create-subnet -c $VPCID -i $DEFAULT_SUBNET1 -z $ZONE`
	SUBNET1_ID=`echo $output | awk '{print $2}'`
else
    echo subet $DEFAULT_SUBNET1 exists
fi

if [ "$SUBNET2" != $DEFAULT_SUBNET2 ]; then
    echo Creating subnet $DEFAULT_SUBNET2
    output=`$EC2_HOME/bin/ec2-create-subnet -c $VPCID -i $DEFAULT_SUBNET2 -z $ZONE`
	SUBNET2_ID=`echo $output | awk '{print $2}'`
else
    echo subet $DEFAULT_SUBNET2 exists
fi

#Setup Routing Table
echo Setting up routing table ... 
RTABLEID=`$EC2_HOME/bin/ec2-describe-route-tables | grep $VPCID | awk '{print $2}'`
$EC2_HOME/bin/ec2-associate-route-table $RTABLEID -s $SUBNET1_ID
$EC2_HOME/bin/ec2-associate-route-table $RTABLEID -s $SUBNET2_ID
$EC2_HOME/bin/ec2-create-route $RTABLEID -r 0.0.0.0/0 -g $IGWID

#Add Rule to Security Group for VPC
echo "Setting up Security Group ..."
SG_ID=`$EC2_HOME/bin/ec2-describe-group | grep VPC  | grep default | awk '{print $2}'`
$EC2_HOME/bin/ec2-authorize $SG_ID -P ICMP -t -1:-1 -s 0.0.0.0/0
$EC2_HOME/bin/ec2-authorize $SG_ID -P TCP -p 22 -s 0.0.0.0/0
$EC2_HOME/bin/ec2-authorize $SG_ID -P TCP -p 80 -s 0.0.0.0/0
$EC2_HOME/bin/ec2-authorize $SG_ID -P TCP -p 8888 -s 0.0.0.0/0
$EC2_HOME/bin/ec2-authorize $SG_ID -P UDP -p 33000-34000 -s 0.0.0.0/0


#Create Network Interfaces
echo Setting up network interfaces
output=`$EC2_HOME/bin/ec2-describe-network-interfaces | grep NETWORKINTERFACE -A 2 | grep $MAMG_IP -A 2`
INTF1_ID=`echo $output | grep NETWORKINTERFACE | awk '{print $2}'`
output=`$EC2_HOME/bin/ec2-describe-network-interfaces $INTF1_ID | grep ATTACHMENT`
ATTACH_INSTANCE=`echo $output | awk '{print $2}'`
ATTACH_ID=`echo $output | awk '{print $3}'`
#echo $INTF1_ID attached to $ATTACH_INSTANCE
if [ -n "$INTF1_ID" ]; then 
    echo Using existing network interface for management: $INTF1_ID
    if [ -n "$ATTACH_INSTANCE" ]; then
        output=`$EC2_HOME/bin/ec2-describe-instances $ATTACH_INSTANCE | grep INSTANCE`
        INS_AMI_ID=`echo $output | awk '{print $3}'`
        if [ $INS_AMI_ID != $AMI_ID ]; then
            echo the network interface is currently attached to $ATTACH_INSTANCE, which is not what we use for lab3, detaching it
            $EC2_HOME/bin/ec2-detach-network-interface $ATTACH_ID -f
        fi
    fi
else
	output=`$EC2_HOME/bin/ec2-create-network-interface $SUBNET1_ID -d "CS144 Lab 3 Primary Network Interface" --private-ip-address $MAMG_IP`
    INTF1_ID=`echo $output | awk '{print $2}'`
	echo "Interface $INTF1_ID created in subnet $DEFAULT_SUBNET1 ($SUBNET1_ID)"
fi

output=`$EC2_HOME/bin/ec2-describe-network-interfaces | grep NETWORKINTERFACE -A 2 | grep $SECOND_IP -A 2`
INTF2_ID=`echo $output | grep NETWORKINTERFACE | awk '{print $2}'`
output=`$EC2_HOME/bin/ec2-describe-network-interfaces $INTF2_ID | grep ATTACHMENT`
INTF2_ATTACH_INSTANCE=`echo $output | awk '{print $2}'`
INTF2_ATTACH_ID=`echo $output | awk '{print $3}'`
#echo $INTF2_ID
if [ -n "$INTF2_ID" ]; then 
    echo Using existing network interface for routing: $INTF2_ID
    if [ -n "$INTF2_ATTACH_INSTANCE" ]; then
        output=`$EC2_HOME/bin/ec2-describe-instances $INTF2_ATTACH_INSTANCE | grep INSTANCE`
        INS_AMI_ID=`echo $output | awk '{print $3}'`
        INS_ID=`echo $output | awk '{print $2}'`
        if [ $INS_AMI_ID != $AMI_ID ]; then
            echo the network interface is currently attached to $INTF2_ATTACH_INSTANCE, which is not what we use for lab3, detaching it
            $EC2_HOME/bin/ec2-detach-network-interface $INTF2_ATTACH_ID -f
        fi
    fi
else
	output=`$EC2_HOME/bin/ec2-create-network-interface $SUBNET2_ID -d "CS144 Lab 3 Second Interface" --private-ip-address $SECOND_IP`
	INTF2_ID=`echo $output | awk '{print $2}'`
	echo "Interface $INTF2_ID created in subnet $DEFAULT_SUBNET2 ($SUBNET2_ID)"
fi

$EC2_HOME/bin/ec2-assign-private-ip-addresses -n $INTF2_ID \
            --secondary-private-ip-address 10.0.1.11 \
            --secondary-private-ip-address 10.0.1.12 \
            --secondary-private-ip-address 10.0.1.13 \
            --secondary-private-ip-address 10.0.1.14 \


