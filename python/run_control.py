import fcntl
import os
import platform
import argparse
#import flare_submit
from astropy.cosmology import Planck15 as cosmo

#This script controls runs for a multidimensional parameter space survey, 
#The survey compares Fisher v Bayes, mergers v not and higher-modes v 22 only. 
#Progress is recorded and new runs are issued based on a status list file

#First step is setting up the parameter cases:
Ms=[ "1e2", "1e4", "1e6" ] 
qs=[ "2", "8" ]
zs=[ "1", "4", "20" ]
#Corresponding distances from astropy.cosmology.Planck15, in Mpc
lum_dist={"1":6791.811049825756, "4":36697.04066923789, "20":230421.93422332808}
modes=[ "lm2", "all" ]
wfs= [ "full", "insp" ]
#orientation: ref (lambda=3pi/4,phi0=pi/3,pol=pi/3,beta=pi/3)
codes=[ "b", "p" ]

#parse ars
parser = argparse.ArgumentParser(description="Control runs for parameter space survey");
parser.add_argument('name',help="The basename for the status list")
parser.add_argument('-g',help="Generate a new status list (must not exist).",action="store_true")
parser.add_argument('-m',help="select modes option "+str(modes),default=modes[0])
parser.add_argument('-z',help="select redshift option "+str(zs),default=zs[0])
parser.add_argument('-c',help="select code option "+str(codes),default=codes[0])
args=parser.parse_args()

#A fresh list of runs is generated by: generate_status_list
#The list will contain run_tag - status pairs
#These are processed by get_next_tag, write_status
#Status list  access is controlled so that only one process can access the file at a time. 
tags = ["new","processing","submitted","running","need_restart","need_analysis","done"]
def read_file_data(fd):
    data=[]
    while(fd):
        line=fd.readline()
        row=(line[:-1].split())
    return data

def write_file_data(fd,data):
    for row in data:
        for s in row:
            fd.write(s)+" "
        fd.write("\n")

def generate_tag(m,q,z,mode,wf,code):
    return m+"_"+q+"_"+z+"_"+mode+"_"+wf+"_"+code+"_ref"

def read_tag(tag):
    keys=["M","q","z","modes","wf","code","pars"]
    vals=tag.split(_)
    return dict(zip(keys,vals))

def generate_status_list(status_file,z,mode,code):
    data=[]
    for M in Ms:
        for q in qs:
            for wf in wfs:
                data.append([generate_tag(M,q,z,mode,wf,code),"new"])
    #open the file but not if it exists
    osfd = os.open(status_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    with os.fdopen(osfd,'w') as fd:
        write_file_data(fd,data)
                             
def get_next_tag(status_file,seek_status):
    with open(status_file,"r") as x:
        fcntl.flock(x, fcntl.LOCK_EX )
        data=read_file_data(x)
        if(seek_status in data[:,1]):
            i=data[:,1].index(seek_status)
            tag=data[i,0]
            data[i,1]="processing"
            x.seek(0);
            x.truncate;
            write_file_data(fd,data)
        else: tag=None
        fcntl.flock(x,fcntl.LOCK_UN)
    return tag

def get_params_string(tag):
    p=read_tag(tag)
    mtot=float(p["M"])
    q=float(p["q"])
    d=cosmo.luminosity_distance(float(p["z"]))
    
                             
    
#detect system
system="discover"
if(platform.system()=="Darwin"):
    system="macos"

statusfile=os.getcwd()+"/"+args.name+"_status_file.txt"

if(args.g):
    generate_status_list(statusfile,args.z,args.m,args.c)
    
