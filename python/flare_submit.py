#use: module load other/SSSO_Ana-PyD/SApd_2.4.0_py2.7
import os
import flare
import argparse

Npar=9

parser = argparse.ArgumentParser(description="generate a discover queue script for a flare run");
parser.add_argument('name',help="The basename for the run")
parser.add_argument('snr',help="SNR for the run")
parser.add_argument('-p',help="List "+str(Npar)+" parameters for the run",nargs=Npar,type=float,required=True)
parser.add_argument('-d',help="Run in debug mode",action="store_true")
parser.add_argument('-m',help="Run with ptmcmc instead of bambi",action="store_true")
parser.add_argument('-n',help="Number of bambi-nodes or ptmcmc-threads to run on (default 1 node or 27 threads)",type=int,default=-1)
parser.add_argument('-t',help="Time in hours to run (default 12, but always 1 with debug)",type=int,default=12)
parser.add_argument('--lm22',help="Use only the 22 mode.",action="store_true")
parser.add_argument('--live',help="Set number of nested sampling live points for bambi default=4000).",default=4000)

args=parser.parse_args()

script="""#!/usr/bin/csh
###This file autogenerated by flare_submit.py  
#SBATCH --job-name=SCRIPT_NAME
#SBATCH --nodes=SCRIPT_NODES --ntasks=SCRIPT_TASKS --constraint=hasw
#SBATCH --time=SCRIPT_HOURS:00:00
#SBATCH --account=s0982
SCRIPT_DEBUG

module purge
module load comp/intel-15.0.3.187 lib/mkl-15.0.3.187 mpi/impi-5.0.3.048
setenv ROM_DATA_PATH /discover/nobackup/jgbaker/GW-DA/flare/ROMdata/q1-12_Mfmin_0.0003940393857519091
set outdir=./SCRIPT_NAME-ntSCRIPT_TASKS
mkdir -p $outdir
cd $outdir
setenv OMP_NUM_THREADS SCRIPT_THREADS;
time mpirun -np SCRIPT_TASKS SCRIPT_COMMAND > SCRIPT_NAME.out

exit 0
"""
#We assume that the script is located in the flare/python directory
#below [:-7] is to stript "/python 
flare_path= os.path.dirname(os.path.realpath(__file__))[:-7]

name=args.name
hours=args.t

#generate the command
if(not args.m):
    if args.n>0:nodes=args.n
    else: nodes=1
    tasks=nodes*24
    threads=1
    cmd   = flare_path+"/LISAinference/LISAinference"
    flags = flare.set_bambi_flags(name,nlive=args.live)
else:
    if args.n>0:threads=args.n
    else: threads=27
    nodes=1
    tasks=1
    cmd   = flare_path+"/LISAinference/LISAinference_ptmcmc"
    flags = flare.set_mcmc_flags(name,60)
#params=draw_params(Mtot,q)
params=args.p
flags+=flare.set_flare_flags(args.snr,params)
if(args.lm22):flags+=" --nbmodeinj 1 --nbmodetemp 1" #for no higher modes in injection and template
cmd = cmd+" "+flags

if args.d:
    debug="#SBATCH --qos=debug\n"
    hours=1
else:
    debug=""

#Replace tags:
#SCRIPT_NAME
script = script.replace("SCRIPT_NAME", name)
#SCRIPT_NODES
script = script.replace("SCRIPT_NODES", str(nodes))
#SCRIPT_HOURS
script = script.replace("SCRIPT_HOURS", str(hours))
#SCRIPT_TASKS
script = script.replace("SCRIPT_TASKS", str(tasks))
#SCRIPT_THREADS
script = script.replace("SCRIPT_THREADS", str(threads))
#SCRIPT_COMMAND
script = script.replace("SCRIPT_COMMAND", cmd)
#SCRIPT_DEBUG
script = script.replace("SCRIPT_DEBUG", debug)

print script

with open(name+".sub",'w') as file:
    file.write(script)
    file.close()

