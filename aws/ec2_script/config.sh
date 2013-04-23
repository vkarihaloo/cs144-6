#!/bin/bash

DEFAULT_VPC_CIDR="10.0.0.0/16"
DEFAULT_SUBNET1="10.0.0.0/24"
DEFAULT_SUBNET2="10.0.1.0/24"
MAMG_IP="10.0.0.100"
SECOND_IP="10.0.1.10"
AMI_ID="ami-83359cea"
RTABLE_FILE="./rtable"
IPTABLE_FILE="./IP_CONFIG"
SSH_FILE="./ec2_ssh.sh"


DIR=`pwd`
export EC2_HOME="$DIR/ec2-api-tools"
export EC2_PRIVATE_KEY=$DIR/credential/pk-ZP4CI4U2ZLJASCVPM7CHTRA7VYBGAKFF.pem
export EC2_CERT=$DIR/credential/cert-ZP4CI4U2ZLJASCVPM7CHTRA7VYBGAKFF.pem
export EC2_KEYPAIR_LOCATION=$DIR/credential/nyn531.pem
export EC2_KEYPAIR_NAME="nyn531"
export JAVA_HOME="/usr/lib/jvm/java-1.6.0-openjdk"
export EC2_URL="http://ec2.us-east-1.amazonaws.com"
export EC2_REGION=us-east-1

chmod 0600 $EC2_KEYPAIR_LOCATION

