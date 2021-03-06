import os
import math
import numpy as np
import subprocess
import re
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
import astropy.units as units
from astropy.cosmology import Planck15 as cosmo,z_at_value
from matplotlib.backends.backend_pdf import PdfPages
import threading
import time
import sys
import traceback
import fmerge

flare_dir=os.path.dirname(os.path.realpath(__file__))[:-7]
multithreaded=True
threadLock=threading.Lock()
ireport=9
FisherRunFailCount=0
noRun=False
all_params_file=False
ROM_DATA_PATH="ROMdata/q1-12_Mfmin_0.0003940393857519091"
extra_flags=""
LISAvariant="LISA2017"
deltatobs=5.0
only22=False;
onlyInspiral=False;
postInspiral=False;
SampleReIm=False;
FisherReIm=True;
ReIm_npts=32768;
linearSNRplot=False;

def set_flare_flags(snr,params):
    flags=""
    #Waveform model
    #flags += " --tagextpn 0" #Don't extend waveforms at low freq to allow lower masses
    if(SampleReIm): flags+=" --tagint 1 --nbptsoverlap " + str(ReIm_npts) #gridded quadrature
    #flags+=" --deltatobs 5.0" #duration in years of LISA observation
    flags+=" --deltatobs "+str(deltatobs) #duration in years of LISA observation
    #flags+=" --minf 1e-4" #minimun frequency included in analysis
    #flags+=" --minf 3e-6" #minimun frequency included in analysis
    minf=3e-6;
    maxf=0.5;
    if(onlyInspiral):
        LSOf=fmerge.Mf(params[0],params[1]) #maximum frequency at LSO
        if(LSOf<maxf):maxf=LSOf;
    if(postInspiral):
        LSOf=fmerge.Mf(params[0],params[1]) #maximum frequency at LSO
        if(LSOf>minf):minf=LSOf;
    flags+=" --maxf "+str(maxf) #maximum frequency included in analysis
    flags+=" --minf "+str(minf) #minimum frequency included in analysis

    #flags+=" --maxf 0.15" #maximum frequency included in analysis
    if(only22):
        flags+=" --nbmodeinj 1 --nbmodetemp 1" #for no higher modes in injection and template
    else:
        flags+=" --nbmodeinj 5 --nbmodetemp 5" #for no higher modes in injection and template
    #set parameter vals
    m1           = params[0]
    m2           = params[1]
    tRef         = params[2]
    phiRef       = params[3]
    if(snr>0):
        dist     = 2e4
    else:
        dist     = params[4]
    lam          = params[5]
    beta         = params[6]
    inc          = params[7]
    pol          = params[8]
    #flags+=" --comp-min 1e5 --comp-max 1e8" #min/max for component mass prior ranges
    #flags+=" --mtot-min 1e4 --mtot-max 1e10 "  #additional prior limits on Mtot and q
    flags+=" --comp-min "+str(m2/3.)+" --comp-max "+str(m1*3.) #min/max factor of 3 for component mass prior ranges
    flags+=" --mtot-min "+str((m1+m2)/10.)+" --mtot-max "+str((m1+m2)*10.)  #min/max factor of 10 for total mass
    
    if(snr>0):
        flags+=" --snr "+str(snr)+" --rescale-distprior" #fixing SNR (rescales distance)
    flags+=" --logflat-massprior" #assume prior uniform in log of masses, rather than uniform for mass."
    #flags+=" --mtot-min 8e5 --mtot-max 2e8 --q-max 11.98"  #additional prior limits on Mtot and q
    #flags+=" --mtot-min 1e4 --mtot-max 1e10 --q-max 11.98"  #additional prior limits on Mtot and q
    flags+=" --q-max 11.98"  #additional prior limits on q
    #flags+=" --dist-min 5000. --dist-max 200e4 --distance 1e5"  #prior range for distances should verify range based on distances (in Mpc).
    #flags+=" --dist-min 1000. --dist-max 4e5"  #prior range for distances approx 0.2<z<33
    flags+=" --dist-min 500. --dist-max 1.3e6"  #prior range for distances approx 0.1<z<100
    flags+=" --flat-distprior" #by default us a flat prior on the distance rather than as R^2
    flags+=" --variant "+LISAvariant #by default us a flat prior on the distance rather than as R^2
    flags+=extra_flags  #Default is none, but can add at runtime...
    #set parameter flags
    flags += " --phiRef "+str(phiRef)
    flags += " --m1 "+str(m1)
    flags += " --m2 "+str(m2)
    flags += " --tRef "+str(tRef)
    flags += " --distance "+str(dist)
    flags += " --lambda "+str(lam)
    flags += " --beta "+str(beta)
    flags += " --inclination "+str(inc)
    flags += " --polarization "+str(pol)
    #print "flags : \n" + flags
    return flags

def set_mcmc_flags(outroot,ptN):
    flags  = ""
    #MCMC basics
    flags += "--noFisher --rng_seed="+str(np.random.rand())
    flags += " --outroot "+str(outroot)
    #flags += " --nskip=40 --info_every=10000" #frequency of sampling/reporting
    flags += " --save_every=40 --info_every=10000" #frequency of sampling/reporting
    flags += " --prop=7 --de_ni=50 --gauss_1d_frac=0.5 --de_reduce_gamma=4" #differential evolution proposal distribution with Gaussian draws 1/2 of the time
    #Parallel Tempering setup
    flags += " --pt_stop_evid_err=0.05 --chain_ess_stop=2000 --pt_Tmax=1e9"    #parallel tempering basics
    if(ptN>0):
        flags += " --pt="+str(ptN) #else default is 20
    flags += " --pt_swap_rate=0.10"   #rate of temp swaps (or default 0.01)
    flags += " --pt_evolve_rate=0.01" #rate at which temps are allowed to evolve

    #flags += " --pt_reboot_rate=0.0001 --pt_reboot_every=10000 --pt_reboot_grace=50000" #Somewhat hacky trick to avoid chains getting stuck.  Not sure whether we need this.
    #stopping criteria
    flags += " --nsteps=100000000" #100 million steps is probably more than we can do
    flags += " --pt_stop_evid_err=0.05" #may terminate earlier based on evidence criterion
    return flags

def set_bambi_flags(outroot,nlive=4000, tol=1.0, multimodal=True):
    flags  = " --nlive "+str(nlive)+" --tol "+str(tol)
    if(multimodal):flags += " --mmodal --nclspar 2 --maxcls 20 --ztol -60"
    flags += " --seed"
    flags += " --outroot "+outroot
    return flags

def par_name(i):
    return ["m1","m2","t0","D","phi0","inc","lambda","beta","pol","sky","orient","Mvol"][i]

def draw_params(Mtot,q):
    #we suppose fixed Mtot,q,SNR and draw the other params
    m1     = Mtot*q/(1.0+q)
    m2     = Mtot/(1.0+q)
    tRef   = np.random.randn()*1e5
    phiRef = 2*math.pi*np.random.rand()
    dist   = 100*10**(np.random.rand()*math.log10(400))
    lam    = np.random.rand()*2.0*math.pi
    beta   = math.acos(np.random.rand()*2.0-1)-math.pi/2.0
    inc    = math.acos(np.random.rand()*2.0-1)
    pol    = np.random.rand()*math.pi
    params = [m1,m2,tRef,phiRef,dist,lam,beta,inc,pol]
    return params

def perform_run(name,Mtot,q,snr):
    if(BAMBI):
        cmd   = flare_dir+"/LISAinference/LISAinference"
        flags = get_bambi_flags(name)
    else:
        cmd   = flare_dir+"/LISAinference/LISAinference_ptmcmc"
        flags = get_mcmc_flags(name,60)
    params=draw_params(Mtot,q)
    flags+=set_flare_flags(snr,params)
    subprocess.call(cmd+" "+flags)

def SNRrun(Mtot,q,snr,name="dummy"):
    cmd   = flare_dir+"/LISAinference/LISAinference_ptmcmc"
    flags = " --nsteps=0 --noFisher"
    params=draw_params(Mtot,q)
    flags+=set_flare_flags(snr,params)
    flags += " --rng_seed="+str(np.random.rand())+" "
    flags += " --outroot "+str(name)+" "
    cmd += " "+flags+">"+name+".out"
    setenv=""

    #setenv = "export ROM_DATA_PATH=/Users/jgbaker/Projects/GWDA/LISA-type-response/flare/ROMdata/q1-12_Mfmin_0.0003940393857519091"
    #setenv="export ROM_DATA_PATH=/discover/nobackup/jgbaker/GW-DA/flare/ROMdata/q1-12_Mfmin_0.0003940393857519091"
    setenv="export ROM_DATA_PATH="+flare_dir+"/"+ROM_DATA_PATH

    print( "Executing '"+cmd+"'")
    code=subprocess.call(setenv+";"+cmd,shell=True)
    print( "Run completed with code(",code,")")
    print( "CWD=",os.getcwd())
    with open(name+"params.txt",'r') as file:
        lines=file.read()
        #print lines
        dist=re.search("dist_resc:(.*)", lines).group(1)
        print( "distance =",dist)
    return float(dist)

def tSNRrun(Mtot,q,snr,name,data):
    cmd   = flare_dir+"/LISAinference/LISAinference_ptmcmc"
    flags = " --nsteps=0 --noFisher"
    params=draw_params(Mtot,q)
    flags+=set_flare_flags(snr,params)
    flags += " --rng_seed="+str(np.random.rand())+" "
    flags += " --outroot "+str(name)+" "
    cmd += " "+flags+">"+name+".out"
    setenv=""
    #setenv = "export ROM_DATA_PATH=/Users/jgbaker/Projects/GWDA/LISA-type-response/flare/ROMdata/q1-12_Mfmin_0.0003940393857519091"
    setenv="export ROM_DATA_PATH="+flare_dir+"/"+ROM_DATA_PATH

    print( "Executing '"+cmd+"'")
    code=subprocess.call(setenv+";"+cmd,shell=True)
    print( "Run completed with code(",code,")")
    with open(name+"params.txt",'r') as file:
        lines=file.read()
        #print lines
        dist=re.search("dist_resc:(.*)", lines).group(1)
        print( "distance =",dist)
    if(not math.isnan(float(dist))):
        data.append(float(dist))
    else:
        print( "NAN_DIST with command:",cmd)
    return

def threadedSNRrun(Mtot,q,snr,label,Nruns,Nthreads,data):
    irun=0
    if(FisherRunFailCount==0):subprocess.call("echo 'Output from runs generating exceptions:' > fisher_fails.out",shell=True)
    while(irun<Nruns):
        if(Nthreads<Nruns-1):count=Nthreads
        else: count=Nruns-irun
        print( "irun=",irun,"Count=",count,"Nruns=",Nruns,"Nthreads=",Nthreads)
        threads=[];
        ith=0
        for t in range(count):
            ith+=1
            thread = threading.Thread(target=tSNRrun, args=(Mtot,q,snr,label+str(ith),data))
            thread.start()
            threads.append(thread)
        for thread in threads:
            thread.join() #this blocks further execution until the thread has returned
        irun += count
        print( " Batch of runs done, now irun=",irun)

def SNRstudy(outlabel,MtotList,qList,SNRList,Navg,Nthreads=1):
    pp = PdfPages(str(outlabel)+'SNRstudy.pdf')
    for q in qList:
        tags=[]
        labels=[]
        count=0
        for snr in SNRList:
            count+=1
            y1=[]
            y2=[]
            x=[]
            for Mtot in MtotList:
                print( "Running SNRrun(",Mtot,",",q,",",snr,")")
                data=[]
                if(multithreaded and Nthreads>1):
                    threadedSNRrun(Mtot,q,snr,outlabel+"dummy",Navg,Nthreads,data)
                else:
                    for i in range(Navg):
                        dist=SNRrun(Mtot,q,snr,outlabel+"dummy")
                        if(not math.isnan(dist)):
                            data.append(dist)
                        else:
                            print( "NAN_DIST!")
                zs=np.zeros(0);
                for dist in data:
                    z=z_at_value(cosmo.luminosity_distance,dist*units.Mpc,zmax=100000,ztol=1e-6)
                    print( "D=",dist," z=",z)
                    zs=np.append(zs,math.log10(z))
                    #zs[i]=math.log10(dist)
                mean=np.mean(zs);
                std=np.std(zs);
                print( "M=",Mtot," q=",q,"dist=",mean,"+/-",std)
                x.append(math.log10(Mtot/(1+10**mean)))
                #x.append(math.log10(Mtot))
                y1.append(mean-std)
                y2.append(mean+std)
            ylabel="log(z)"
            ylim=[-1,3]
            xlim=[2,9]
            if(linearSNRplot):
                y1=[10**y for y in y1]
                y2=[10**y for y in y2]
                ylabel="z"
                ylim=[0,20]
                xlim=[3.5,8.5]
            print( "x=",x)
            print( "y1=",y1)
            print( "y2=",y2)
            color=(0.2,0.8/math.sqrt(q),1.0/math.sqrt(count))
            plot=plt.fill_between(x, y1, y2, facecolor=color,alpha=0.3, interpolate=True)
            tags.append( Rectangle((0, 0), 1, 1, fc=color,alpha=0.3) )
            labels.append("SNR="+str(snr))
            print( "Finished band for SNR="+str(snr))
        plt.legend(tags,labels)
        plt.ylim(ylim)
        plt.xlim(xlim)
        plt.title("SNR contours for LISA q="+str(q)+" SMBH merger")
        plt.ylabel(ylabel)
        plt.xlabel("log(M/Msun)")
        #plt.show()
        pp.savefig()
        plt.clf()
        print( "Finished plot for q="+str(q))
    pp.close()

def getFisherCommand(label,delta=0.1,extrapoints=1.0):
    cmd   = flare_dir+"/LISAinference/LISAinference_ptmcmc"
    #npts= int(extrapoints*20/delta/delta) #Simplified variant, multiplied by additional factor of two to be conservative note seems we only have convergence at order (1/nbpts)^0.5 I
    npts = ReIm_npts
    flags = "--nsteps=0 --Fisher_err_target="+str(delta)+" --flat-distprior --deltaT 5000"
    if(FisherReIm):
        flags+=" --tagint 1 --nbptsoverlap "+str(npts)
    name=str(label)
    flags += " --rng_seed="+str(np.random.rand())+" "
    flags += " --outroot "+str(name)+" "
    cmd += " "+flags
    return cmd;

def FisherRunByParams(snr,params,delta,label,extrapoints=1.0):
    flags=set_flare_flags(snr,params)
    cmd   = getFisherCommand(label,delta)+" "+flags+" >"+str(label)+".out";
    setenv="export ROM_DATA_PATH="+flare_dir+"/"+ROM_DATA_PATH

    try:
        print( "Executing '"+cmd+"'")
        dist=0
        cov=[]
        if not noRun:
            code=subprocess.call(setenv+";"+cmd,shell=True)
            print( "Run "+name+" completed with code(",code,")")
            with open(name+"params.txt",'r') as file:
                lines=file.read()
                #print lines
                ##
                print( name+"params.txt")
                dist=re.search("dist_resc:(.*)", lines).group(1)
                print( "distance =",dist)
            time.sleep(1)#pause to make sure file is ready to read.
            cov=readCovarFile(name+"_fishcov.dat")
            #v=math.sqrt(np.random.rand()-0.1)#to test behavior under occasional failures
    except (ValueError,ArithmeticError):
        print( "Exception",sys.exc_info()[0]," occurred in run"+name+" for params ",params)
        FisherRunFailCount+=1
        print( "  FailCount=",FisherRunFailCount)
        subprocess.call("echo '\n\n***********************\nFailure "+str(FisherRunFailCount)+"\n***********************\n"+cmd+"' |cat - "+name+".out "+name+"_fishcov.out >> fisher_fails.out",shell=True)
    return [float(dist)]+cov

def FisherRun(Mtot,q,snr,delta,label,data,extrapoints=1.0):
    global FisherRunFailCount
    params=draw_params(Mtot,q)
    if(not getattr(all_params_file,"write",None)==None):
        threadLock.acquire()
        all_params_file.write(str(snr)+"\t")
        for pval in params:
            all_params_file.write(str(pval)+"\t")
        all_params_file.write("\n")
        threadLock.release()
    datum=FisherRunByParams(snr,params,delta,label,extrapoints)
    data.append( datum )
    return

def threadedFisherRun(Mtot,q,snr,delta,label,Nruns,Nthreads,data,extrapoints):
    irun=0
    if(FisherRunFailCount==0):subprocess.call("echo 'Output from runs generating exceptions:' > fisher_fails.out",shell=True)
    while(irun<Nruns):
        if(Nthreads<Nruns-1):count=Nthreads
        else: count=Nruns-irun
        print( "irun=",irun,"Count=",count,"Nruns=",Nruns,"Nthreads=",Nthreads)
        threads=[];
        ith=0
        for t in range(count):
            ith+=1
            thread = threading.Thread(target=FisherRun, args=(Mtot,q,snr,delta,label+str(ith),data,extrapoints))
            thread.start()
            threads.append(thread)
        for thread in threads:
            thread.join() #this blocks further execution until the thread has returned
        irun += count
        print( " Batch of runs done, now irun=",irun)

def readCovarFile(file):
    pars=[]
    done=False
    trycount=0
    while not done:
        try:
            with open(file,'r') as f:
                line="#"
                while("#" in line): line=f.readline() #Skip comment
                for val in line.split():
                    #print val
                    pars.append(float(val))
                Npar=len(pars)
                while(not "#Covariance" in line):line=f.readline() #Skip until the good stuff
                covar=np.zeros((Npar,Npar))
                i=0
                for par in pars:
                    line=f.readline()
                    covar[i]=np.array(line.split())
                    i+=1
                inc    = pars[5] #runs from 0 to pi at poles
                #lam    = pars[6]
                beta   = pars[7] #runs from -pi/2 to pi/2 at poles
                #pol    = pars[8]
                val=covar[0][0]
                if val<0: dm1=float('nan')
                else: dm1=math.sqrt(val)
                val=covar[1][1]
                if val<0: dm2=float('nan')
                else: dm2=math.sqrt(val)
                dtRef  = math.sqrt(covar[2][2])
                dD     = math.sqrt(covar[3][3])
                dphase = math.sqrt(covar[4][4])
                dinc   = math.sqrt(covar[5][5])
                dlam   = math.sqrt(covar[6][6])
                dbeta  = math.sqrt(covar[7][7])
                dpol   = math.sqrt(covar[8][8])
                val=covar[6][6]*covar[7][7]-covar[6][7]**2
                if val<0:
                    dsky=float('nan')
                    if -val<1e-13*covar[6][6]*covar[7][7]:dsky=0
                else: dsky=math.sqrt(val)*math.cos(pars[7])
                print( "sky",val,dsky,covar[6][6],covar[7][7])
                val=covar[5][5]*covar[8][8]-covar[5][8]**2
                if val<0:
                    dori=float('nan')
                    if -val<1e-13*covar[5][5]*covar[8][8]:dori=0
                else: dori=math.sqrt(val)*math.sin(pars[5])
                #HACK? need to verify factor of sin(inc) here!
                val=covar[0][0]*covar[1][1]-covar[0][1]**2
                if val<0:
                    dmvol=float('nan')
                    if -val<1e-13*covar[0][0]*covar[1][1]:dmvol=0
                else: dmvol=math.sqrt(val)
            done=True
        except EnvironmentError:
            print( "Something went wrong in trying to open covariance file:",sys.exc_info()[0])
            print( "Try=",trycount)
            subprocess.call("ls -ort")
            trycount+=1;
            if(trycount>10):
                print( "giving up!!!")
                done=True
                raise
        except ValueError:
            print( traceback.format_exc(limit=1))
            print( "Continuing after arithmetic error:")
        #else: print "...No execption in read covar"
            raise
    return [dm1,dm2,dtRef,dD,dphase,dinc,dlam,dbeta,dpol,dsky,dori,dmvol]
            
def writeFisherSamples(name,numSamples):
    fishcov=name+"_fishcov.dat"
    outsamp=name+"_fishcov.dat"
    with open(fishcov,'r') as f:
        line="#"
        while("#" in line): line=f.readline() #Skip comment
        for val in line.split():
            #print val
            pars.append(float(val))
        Npar=len(pars)
        while(not "#Covariance" in line):line=f.readline() #Skip until the good stuff
        covar=np.zeros((Npar,Npar))
        i=0
        for par in pars:
            line=f.readline()
            covar[i]=np.array(line.split())
            i+=1
    with open(outsamp,'w') as f:
        for i in range(numSamples):
            f.write("0  0  0  0  0 ")
            dpars=np.random.multivariate_normal(pars,covar)
            for val in pars+dpars:
                f.write("  "+str(val))
            f.write("\n")
    
    
        
        
def FisherStudy(outlabel,MtotList,qList,SNRList,deltalist,Navg,Nthreads,extrapoints=1.0):
    pp = PdfPages(str(outlabel)+'FisherStudy.pdf')
    datafile = open(outlabel+'FisherStudy.dat','w')
    for q in qList:
        tags=[]
        labels=[]
        snrcount=0
        for snr in SNRList:
            snrcount+=1
            deltacount=0;
            for delta in deltalist:
                deltacount+=1
                y1=[]
                y2=[]
                x=[]
                for Mtot in MtotList:
                    data=[]
                    logzs=np.zeros(Navg);
                    print( "Running FisherRun(",Mtot,",",q,",",snr,")")
                    if(multithreaded):
                        threadedFisherRun(Mtot,q,snr,delta,outlabel+"dummy",Navg,Nthreads,data,extrapoints)
                    else:
                        for i in range(Navg):
                            FisherRun(Mtot,q,snr,delta,outlabel+"dummy",data,extrapoints)
                    if noRun: continue
                    for i in range(Navg):
                        distance=cosmo.luminosity_distance,data[i][0]*units.Mpc
                        print( "distance=",distance)
                        z=z_at_value(cosmo.luminosity_distance,data[i][0]*units.Mpc,zmax=100000,ztol=1e-6)
                        #print "D=",dist," z=",z
                        logzs[i]=math.log10(z)
                    meanz=np.mean(logzs);
                    stdz=np.std(logzs);
                    print( "M=",Mtot," q=",q,"z=",meanz,"+/-",stdz)
                    datafile.write(str(snr)+"\t"+str(delta)+"\t"+str(Mtot)+"\t"+str(q)+"\t"+str(meanz)+"\t"+str(stdz))
                    npdata=np.array(data)
                    print( "data:\n",data)
                    print( "npdata:\n",npdata)
                    Nstats=len(npdata[0,:])-1
                    print( "Nstats=",Nstats)
                    #i=0
                    #for d in [npdata[j,:] for j in range(Navg) ]:
                    #    print "d:\n",d
                    #    print "len[",i,"]=",d.size
                    #    i+=1
                    #    if not d.size==Nstats+1:
                    #        print "wrong length for:\n ",d
                    means=[]
                    stds=[]
                    for i in range(Nstats):
                        print( "i=",i)
                        sel=npdata[:,i+1]
                        v=np.log10(np.array(sel))
                        mean=np.mean(v)
                        std=np.std(v)
                        means.append(mean)
                        stds.append(std)
                        print("  ",mean," +/- ",std)
                        datafile.write("\t"+str(mean)+"\t"+str(std))
                    datafile.write("\n")
                    datafile.flush()
                    x.append(math.log10(Mtot/(1+10**meanz)))
                    #x.append(math.log10(Mtot))
                    #y1.append(meanz-stdz)
                    #y2.append(meanz+stdz)
                    y1.append((means[ireport]-stds[ireport]))
                    y2.append((means[ireport]+stds[ireport]))
                print( "x=",x)
                print( "y1=",y1)
                print( "y2=",y2)
                color=(0.2,0.8/math.sqrt(q),1.0/math.sqrt(snrcount))
                if(deltacount==1):
                    plot=plt.fill_between(x, y1, y2, facecolor=color,alpha=0.3, interpolate=True)
                else:
                    plot=plt.plot(x,y1,color=color,alpha=1.0)
                    plot=plt.plot(x,y2,color=color,alpha=1.0)
            tags.append( Rectangle((0, 0), 1, 1, fc=color,alpha=0.3) )
            labels.append("SNR="+str(snr))
        plt.legend(tags,labels)
        plt.ylim([-1,7])
        plt.xlim([3,9])
        plt.title("Parameter uncertainty for LISA q="+str(q)+" SMBH merger")
        plt.ylabel("log(d"+par_name(ireport)+")")
        plt.xlabel("log(M/Msun)")
        #plt.show()
        pp.savefig()
        plt.clf()
    pp.close()



def FisherPlot(outlabel,ipar,qList,SNRList,deltalist,datafile,scaled=False,targetSNR=None,errorNsigma=2):
    pp = PdfPages(str(outlabel)+'Fisher-'+par_name(ipar)+'.pdf')
    #datafile = open(datafile,'r')
    tol=1e-10
    data=np.loadtxt(datafile)
    punits=["Msun","Msun","s","Mpc","rad","rad","rad","rad","rad",r"$rad^2$",r"$rad^2$",r"$Msun^2$"]
    sunits=["m1",  "m2",  "s","D",  "rad","rad","rad","rad","rad",r"$rad^2$",r"$rad^2$",r"(m1*m2)"]
    if(scaled):punits=sunits
    for q in qList:
        tags=[]
        labels=[]
        snrcount=0
        for snr in SNRList:
            if(targetSNR==None):targetSNR=snr
            snrcount+=1
            deltacount=0;
            for delta in deltalist:
                deltacount+=1
                subdata=[]
                for d in data:
                    if abs(d[0]/snr-1)<tol and abs(d[1]/delta-1)<tol and abs(d[3]/q-1)<tol:
                        subdata.append(d)
                subdata=np.array(subdata)
                print( "subdata=",subdata)
                iMtot=2
                imeanz=4
                istdz=5
                imeanpar=6+ipar*2
                istdpar=7+ipar*2
                    #x.append(math.log10(Mtot/(1+10**meanz)))
                    #y1.append((means[ireport]-stds[ireport]))
                    #y2.append((means[ireport]+stds[ireport]))
                #compute scalings, if needed
                scales=subdata[:,iMtot]*0 #initially set all (logs of) scales to 0.0
                if(scaled):#note all param error data are in log-space here
                    if(ipar==0): #scale by m1
                        scales=np.log10(subdata[:,iMtot]/(1+1/q))
                    if(ipar==1): #scale by m2
                        scales=np.log10(subdata[:,iMtot]/(1+q))
                    if(ipar==3): #scale by distance as computed from mean z
                        scales=np.array([math.log10(cosmo.luminosity_distance(10.0**zz).value) for zz in subdata[:,imeanz]])
                    if(ipar==11): #scale by m1*m2
                        scales=np.log10(subdata[:,iMtot]*subdata[:,iMtot]/(1+q)/(1+1/q))

                meanzarray=subdata[:,imeanz]
                SNRrescale_factor=1.0
                if(not targetSNR==snr):
                    SNRrescale_factor=targetSNR/snr
                    #next make a new array of redshifts znew=z(D(z)/SNRrescale_factor)
                    meanzarray=np.array([math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/SNRrescale_factor),zmax=100000,ztol=1e-6)) for zz in meanzarray])
                    print( "Rescaling SNR by ",SNRrescale_factor," from ",snr," to ", targetSNR)
                x=[ math.log10(a/(1+10**b)) for a,b in zip(subdata[:,iMtot],meanzarray) ]
                #print "x=",x
                #print "imeanpar=",imeanpar
                #print "y0=",subdata[:,imeanpar]
                #print "dy=",subdata[:,istdpar]
                y1=subdata[:,imeanpar]-subdata[:,istdpar]-scales + math.log10(1.0*errorNsigma/SNRrescale_factor)
                #print "y1=",y1
                y2=subdata[:,imeanpar]+subdata[:,istdpar]-scales + math.log10(1.0*errorNsigma/SNRrescale_factor)
                #print "y2=",y2
                color=(0.2,0.8/math.sqrt(q),1.0/math.sqrt(snrcount))
                if(deltacount==1):
                    plot=plt.fill_between(x, y1, y2, facecolor=color,alpha=0.3, interpolate=True)
                else:
                    plot=plt.plot(x,y1,color=color,alpha=1.0)
                    plot=plt.plot(x,y2,color=color,alpha=1.0)
                #plt.plot(x,scales)
                tags.append( Rectangle((0, 0), 1, 1, fc=color,alpha=0.3) )
            labels.append("SNR="+str(targetSNR))
        plt.legend(tags,labels)
        #plt.ylim([-1,7])
        plt.xlim([3,9])
        plt.title("Parameter uncertainty for LISA q="+str(q)+" SMBH merger")
        plt.ylabel("log("+str(errorNsigma)+r"$\sigma$"+"["+par_name(ipar)+"]/"+punits[ipar]+")")
        plt.xlabel("log(M/Msun)")
        #plt.show()
        pp.savefig()
        plt.clf()
    pp.close()


def HorizonPlot(outlabel,ipar,qList,snr,delta,datafile,horizonlist,scaled=False,errorNsigma=2,show_range=False):
    rangetag=''
    if(show_range):rangetag='range-'
    name=str(outlabel)+'Horizon-'+rangetag+par_name(ipar)+'.pdf'
    print( "Making plot: "+name)
    pp = PdfPages(name);
    #datafile = open(datafile,'r')
    tol=1e-10
    data=np.loadtxt(datafile)
    punits=["Msun","Msun","s","Mpc","rad","rad","rad","rad","rad",r"$rad^2$",r"$rad^2$",r"$Msun^2$"]
    sunits=["m1",  "m2",  "s","D",  "rad","rad","rad","rad","rad",r"$deg^2$",r"$rad^2$",r"(m1*m2)"]

    if(scaled):punits=sunits
    for q in qList:
        tags=[]
        labels=[]
        subdata=[]
        print( "finding data with SNR="+str(snr)+", delta="+str(delta)+", q="+str(q))
        for d in data:
            if abs(d[0]/snr-1)<tol and abs(d[1]/delta-1)<tol and abs(d[3]/q-1)<tol:
                subdata.append(d)
        subdata=np.array(subdata)
        print( "subdata=",subdata)
        iMtot=2
        imeanz=4
        istdz=5
        imeanpar=6+ipar*2
        istdpar=7+ipar*2
        #compute scalings, if needed
        scales=subdata[:,iMtot]*0 #initially set all (logs of) scales to 0.0
        if(scaled):#note all param error data are in log-space here
            if(ipar==0): #scale by m1
                scales=np.log10(subdata[:,iMtot]/(1+1/q))
            if(ipar==1): #scale by m2
                scales=np.log10(subdata[:,iMtot]/(1+q))
            if(ipar==3): #scale by distance as computed from mean z
                scales=np.array([math.log10(cosmo.luminosity_distance(10.0**zz).value) for zz in subdata[:,imeanz]])
            if(ipar==9): #scale by arcmin^2
                scales=np.full_like(subdata[:,iMtot], 2*math.log10(math.pi/180.0))
            if(ipar==11): #scale by m1*m2
                scales=np.log10(subdata[:,iMtot]*subdata[:,iMtot]/(1+q)/(1+1/q))
        print( scales)
        colorcount=0
        for horizoncut in horizonlist:
            colorcount+=1
            meanzarray=subdata[:,imeanz]
            stdpararray=subdata[:,istdpar]
            testvalues=10**(subdata[:,imeanpar]-scales + math.log10(1.0*errorNsigma))
            SNRrescale_factors=testvalues/horizoncut
            dSNRrescale_factors=10**stdpararray;
            if(ipar>=9):#derived quadratic scaled stats
                SNRrescale_factors=np.sqrt(SNRrescale_factors)
                dSNRrescale_factors=np.sqrt(dSNRrescale_factors)
            midzarray=np.array([math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/fac),zmax=100000,ztol=1e-6)) for zz,fac in zip(meanzarray,SNRrescale_factors)])
            if(show_range):
                topzarray=np.array([math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/fac),zmax=100000,ztol=1e-6)) for zz,fac in zip(meanzarray,SNRrescale_factors/dSNRrescale_factors)])
                botzarray=np.array([math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/fac),zmax=100000,ztol=1e-6)) for zz,fac in zip(meanzarray,SNRrescale_factors*dSNRrescale_factors)])

            x=[ math.log10(a/(1+10**b)) for a,b in zip(subdata[:,iMtot],midzarray) ]
            color=(1.0-colorcount/(len(horizonlist)+1.0),0.8/math.sqrt(q),colorcount/(len(horizonlist)+1.0))
            if(show_range):
                plot=plt.fill_between(x, botzarray, topzarray, facecolor=color,alpha=0.3, interpolate=True)
                color=np.array(color)*0.8
            plot=plt.plot(x,midzarray,color=color,alpha=1.0)
            tags.append( Rectangle((0, 0), 1, 1, fc=color,alpha=0.3) )
            labels.append(str(horizoncut)+"="+str(errorNsigma)+r"$\sigma$"+"["+par_name(ipar)+"]/"+punits[ipar])
        plt.legend(tags,labels)
        plt.ylim([-1,3])
        plt.xlim([3,9])
        plt.title("Science range for "+outlabel+" q="+str(q)+" SMBH merger")
        plt.ylabel("log(z)")
        plt.xlabel("log(M/Msun)")
        #plt.show()
        pp.savefig()
        plt.clf()
    pp.close()


def HorizonCompare(outlabel,ipar,qList,snr,delta,datafiles,horizonlist,scaled=False,errorNsigma=2):
    rangetag=''
    pp = PdfPages(str(outlabel)+'Horizon-'+rangetag+par_name(ipar)+'.pdf')
    #datafile = open(datafile,'r')
    tol=1e-10
    datalist=[]
    for datafile in datafiles:
        datalist.append(np.loadtxt(datafile))
    punits=["Msun","Msun","s","Mpc","rad","rad","rad","rad","rad",r"$rad^2$",r"$rad^2$",r"$Msun^2$"]
    sunits=["m1",  "m2",  "s","D",  "rad","rad","rad","rad","rad",r"$deg^2$",r"$rad^2$",r"(m1*m2)"]

    if(scaled):punits=sunits
    for q in qList:
        tags=[]
        labels=[]
        subdatalist=[]
        for data in datalist:
            subdata=[]
            for d in data:
                if abs(d[0]/snr-1)<tol and abs(d[1]/delta-1)<tol and abs(d[3]/q-1)<tol:
                    subdata.append(d)
            subdatalist.append(subdata)
        subdata=np.array(subdata)
        print( "subdata=",subdata)
        iMtot=2
        imeanz=4
        istdz=5
        imeanpar=6+ipar*2
        istdpar=7+ipar*2
        #compute scalings, if needed
        #scales=subdata[:,iMtot]*0 #initially set all (logs of) scales to 0.0
        scaleslist=subdatalist[:,:,iMtot]*0 #initially set all (logs of) scales to 0.0
        if(scaled):#note all param error data are in log-space here
            if(ipar==0): #scale by m1
                scaleslist=np.log10(subdatalist[:,:,iMtot]/(1+1/q))
            if(ipar==1): #scale by m2
                scaleslist=np.log10(subdatalist[:,:,iMtot]/(1+q))
            if(ipar==3): #scale by distance as computed from mean z
                scaleslist=np.array([[math.log10(cosmo.luminosity_distance(10.0**zz).value) for zz in subdata[:,imeanz]] for subdata in subdatalist])
            if(ipar==9): #scale by arcmin^2
                scaleslist=np.full_like(subdatalist[:,:,iMtot], 2*math.log10(math.pi/180.0))
            if(ipar==11): #scale by m1*m2
                scaleslist=np.log10(subdatalist[:,:,iMtot]*subdatalist[::,iMtot]/(1+q)/(1+1/q))
        print( scales)        
        colorcount=0
        for horizoncut in horizonlist:                   
            colorcount+=1
            meanzarraylist=subdatalist[:,:,imeanz]
            stdpararraylist=subdatalist[:,:,istdpar]
            testvalueslist=10**(subdatalist[:,:,imeanpar]-scales + math.log10(1.0*errorNsigma))
            SNRrescale_factorslist=testvalueslist/horizoncut
            dSNRrescale_factorslist=10**stdpararraylist;
            if(ipar>=9):#derived quadratic scaled stats
                SNRrescale_factorslist=np.sqrt(SNRrescale_factorslist)
                dSNRrescale_factorslist=np.sqrt(dSNRrescale_factorslist)
            #midzarray=np.array([math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/fac),zmax=100000,ztol=1e-6)) for zz,fac in zip(meanzarray,SNRrescale_factors)])
            midzarraylist=np.array([[math.log10(z_at_value(cosmo.luminosity_distance,cosmo.luminosity_distance(10**zz/fac),zmax=100000,ztol=1e-6)) for zz,fac in pairs ] for pairs in np.dstack(meanzarray,SNRrescale_factors)])
            x=[ [ math.log10(a/(1+10**b)) for a,b in pairs ] for pairs in np.dstack(subdata[:,iMtot],midzarray) ]
            color=(1.0-colorcount/(len(horizonlist)+1.0),0.8/math.sqrt(q),colorcount/(len(horizonlist)+1.0))
            for ifile in range(len(datafilelist)):
                plt.plot(x[ifile],midzarray[ifile],color=color,alpha=1.0)
            tags.append( Rectangle((0, 0), 1, 1, fc=color,alpha=0.3) )
            labels.append(str(horizoncut)+"="+str(errorNsigma)+r"$\sigma$"+"["+par_name(ipar)+"]/"+punits[ipar]+" ("+datafilelist[ifile]+")" )
        plt.legend(tags,labels)
        plt.ylim([-1,3])
        plt.xlim([3,9])
        plt.title("Science range for "+outlabel+" q="+str(q)+" SMBH merger")
        plt.ylabel("log(z)")
        plt.xlabel("log(M/Msun)")
        #plt.show()
        pp.savefig()
        plt.clf()
    pp.close()

