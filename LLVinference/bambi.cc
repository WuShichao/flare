#ifdef PARALLEL
#include "mpi.h"
#endif
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include <stdlib.h>
#include "NNopt.h"
#include "bambi.h"

/****************************************/
int myid = 0,nproc=1;
char root[1000],networkinputs[1000];
bool likenetinit=false,converged=false,lastconverged=false,netres,firstrun=true,discardpts=true;
int ignoredbambicalls=0;
int counter=0;
size_t nlayers,nnodes[10];
int doBAMBI=0,useNN=0,whitenin,whitenout,resume;
float thL[3],logLRange;
//std::vector <NN_args> args;
//std::vector <float> omicron;
//std::vector <double> maxsigma;
float *omicron;
double *maxsigma;
int nNN=0,nNNerr=0;
int totpar;
int loglcalls=0;

int ncheck=1000;
float tol=1.0;
double logZero;
/****************************************/

NeuralNetwork *nn, ***nnstore;
histEqualTransform otrans;
NN_args *args;
int npars2;
void *context2;

extern "C" void getLogLike(double *Cube, int *ndim, int *npars, double *lnew, void *context);
extern "C" void getphysparams(double *Cube, int *ndim, int *nPar, void *context);
extern "C" void getallparams(double *Cube, int *ndim, int *nPar, void *context);


/***********************************************************************************************************************/

// Input arguments
// ndim 						= dimensionality (total number of free parameters) of the problem
// npars 						= total number of free plus derived parameters
//
// Input/Output arguments
// Cube[npars] 						= on entry has the ndim parameters in unit-hypercube
//	 						on exit, the physical parameters plus copy any derived parameters you want to store with the free parameters
//	 
// Output arguments
// lnew 						= loglikelihood

void LogLike(double *Cube, int *ndim, int *npars, double *lnew, void *context)
{
	static int thcounter[2];
	int thcheck[] = {18, 20};
	
	if( firstrun )
	{
#ifdef PARALLEL
		MPI_Comm_rank(MPI_COMM_WORLD,&myid);
		MPI_Comm_size(MPI_COMM_WORLD,&nproc);
#endif
		
		npars2=(*npars);
		context2=context;
		
		if( doBAMBI==1 || useNN==1 || useNN==2 )
		{
			if( resume )
				netres=true;
			else
				netres=false;
		
			if( netres || useNN==1 || useNN==2 )
				FirstRunCheck(*ndim);
			else
				GetNetTol();
		
			if( whitenin==3 || whitenout==3 ) netres=false;
			
			thcounter[0]=thcounter[1]=0;
		}
	}
	firstrun=false;
	
	double CubeOrig[*npars];
	for( int i=0;i<*npars;i++ ) CubeOrig[i]=Cube[i];
	
	if( doBAMBI==0 && useNN==0 )
	{
		getLogLike(Cube, ndim, npars, lnew, context);
		return;
	}
	else if( useNN==1 || useNN==2 || ( doBAMBI==1 && converged ) )
	{
		getphysparams(Cube,ndim,npars,context);
	}
	
	float netL=0.;
	
	// Evaluate likelihood with a network if one is converged
	if( doBAMBI==1 && converged )
	{
		float FCube[*npars];
		for (size_t i=0; i<*npars; i++) FCube[i]=(float)Cube[i];
		nn->forwardOne(1,FCube,&netL);
		if( whitenout==3 ) otrans.inverse(&netL,&netL,1);
		
		if( netL>thL[1] )
		{
			printf("Prediction %g outside bounds (%g, %g).\n",netL,thL[0],thL[1]);
			for( int i = 0; i < *npars; i++ ) Cube[i] = CubeOrig[i];
			double DnetL;
			getLogLike(Cube, ndim, npars, &DnetL, context);
			netL=(float)DnetL;
			/*converged=false;
			thcounter[0] = thcounter[1] = 0;*/
		}
		
		if( converged )
		{
			if( netL>=thL[2] )
			{
				thcounter[0]++;
				if( thcounter[0] >= thcheck[0] )
				{
					printf("%d of last %d logL evaluations from NN were higher than the 95%% threshold of %g. Will have to train again.\n",thcounter[0],thcounter[1]+1,thL[2]);
					converged=false;
					for( int i=0;i<*ndim;i++ ) Cube[i]=CubeOrig[i];
					thcounter[0] = thcounter[1] = 0;
				}
			}
			thcounter[1]++;
		
			if( thcounter[1] >= thcheck[1] ) thcounter[0] = thcounter[1] = 0;
		}
	}
	else if( useNN==1 )
	{
		float netlogL[nNN][1], sigma[nNN][1];
		std::vector <float> inputs(*ndim), outputs(1, 0.0);
		std::vector <size_t> ntime(1, 1), ntimeignore(1, 0);
		//float inputs[ndim],outputs[1];
		//outputs[0]=0.;
		//size_t ntime[1],ntimeignore[1];
		//ntime[0]=1;
		//ntimeignore[0]=0;
		for( int j=0; j<*ndim; j++ ) inputs[j]=(float)Cube[j];
		TrainingData vd(false, *ndim, 1, 1, inputs, outputs, ntime, ntimeignore);
		int best = -1;
		float bestfsigma=1E6;
		
		for( int i=nNN-1; i>=0; i-- )
		{
			for( size_t j = 0; j < vd.cumoutsets[vd.ndata]*vd.nout; j++ ) vd.acc[j]=args[i].td->acc[j];
			PredictedData pvd(args[i].nn->totnnodes, args[i].nn->totrnodes, vd.ndata, vd.nin, vd.nout, vd.cuminsets[vd.ndata], vd.cumoutsets[vd.ndata]);
			
			NN_args vargs;
			vargs.np=args[i].np;
			vargs.td=&vd;
			vargs.pd=&pvd;
			vargs.nn=args[i].nn;
			vargs.alphas=args[i].alphas;
			vargs.prior=args[i].prior;
			vargs.noise=args[i].noise;
			vargs.nalphas=args[i].nalphas;
			vargs.Bcode=args[i].Bcode;
			std::vector <float> s, netllike;
			
			GetPredictions(&vargs, &args[i], 0, omicron[i], false, netllike, s, true);
			netlogL[i][0]=netllike[0];
			sigma[i][0]=s[0];

			if( sigma[i][0]<1.2*maxsigma[i] )
			{
				best=i;
				break;
			}
			/*else
			{
				double fsigma=sigma[i][0]/maxsigma[i];
				if( fsigma<bestfsigma )
				{
					bestfsigma=fsigma;
					best=i;
				}
			}*/
		}
		
		if( best==-1 )
		{
			if( discardpts )
			{
				*lnew=logZero;
				for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
				getallparams(Cube,ndim,npars,context);
			}
			else
			{
				for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
				getLogLike(Cube, ndim, npars, lnew, context);
				loglcalls++;
			}
		}
		else
		{
			*lnew=netlogL[best][0];
			for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
			getallparams(Cube,ndim,npars,context);
		}
		return;
	}
	else if( useNN==2 )
	{
		//std::vector <float> inputs(ndim);
		float inputs[*ndim];
		float output[1];
		double netlogL[nNN][2];
		for( int j=0; j<*ndim; j++ ) inputs[j]=(float)Cube[j];
		for( int j=0; j<nNN; j++ ) netlogL[j][0]=netlogL[j][1]=0.0;
		int best=-1;
		
		for( int i=nNN-1; i>=0; i-- )
		{
			for( int j=0; j<nNNerr; j++ )
			{
				nnstore[i][j]->forwardOne(1,&inputs[0],output);
				if( whitenout==3 ) otrans.inverse(output,output,1);
				netlogL[i][0]+=(double)output[0];
				netlogL[i][1]+=(double)output[0]*(double)output[0];
			}
			netlogL[i][0]/=(double)nNNerr;
			netlogL[i][1]=sqrt(netlogL[i][1]/(double)nNNerr-netlogL[i][0]*netlogL[i][0]);
			
			if( netlogL[i][1]<1.2*maxsigma[i] )
			{
				best=i;
				break;
			}
		}
		
		if( best==-1 )
		{
			if( discardpts )
			{
				*lnew=logZero;
				for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
				getallparams(Cube,ndim,npars,context);
			}
			else
			{
				for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
				getLogLike(Cube, ndim, npars, lnew, context);
				loglcalls++;
			}
		}
		else
		{
			*lnew=netlogL[best][0];
			for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
			getallparams(Cube,ndim,npars,context);
		}
		return;
	}
	else
	{
		thcounter[0] = thcounter[1] = 0;
	}
	
	// Evaluate original likelihood when network isn't converged and for periodic checks
	if( !converged )
	{
		getLogLike(Cube, ndim, npars, lnew, context);
		return;
	}
	else
	{
		*lnew=(double)netL;
	}
		
	for( int i=0;i<*npars;i++ ) Cube[i]=CubeOrig[i];
	getallparams(Cube,ndim,npars,context);
}

/***********************************************************************************************************************/




/************************************************* dumper routine ******************************************************/

// The dumper routine will be called every updInt*10 iterations
// MultiNest doesn not need to the user to do anything. User can use the arguments in whichever way he/she wants
//
//
// Arguments:
//
// nSamples 						= total number of samples in posterior distribution
// nlive 						= total number of live points
// nPar 						= total number of parameters (free + derived)
// physLive[1][nlive * (nPar + 1)] 			= 2D array containing the last set of live points (physical parameters plus derived parameters) along with their loglikelihood values
// posterior[1][nSamples * (nPar + 2)] 			= posterior distribution containing nSamples points. Each sample has nPar parameters (physical + derived) along with the their loglike value & posterior probability
// paramConstr[1][4*nPar]:
// paramConstr[0][0] to paramConstr[0][nPar - 1] 	= mean values of the parameters
// paramConstr[0][nPar] to paramConstr[0][2*nPar - 1] 	= standard deviation of the parameters
// paramConstr[0][nPar*2] to paramConstr[0][3*nPar - 1] = best-fit (maxlike) parameters
// paramConstr[0][nPar*4] to paramConstr[0][4*nPar - 1] = MAP (maximum-a-posteriori) parameters
// maxLogLike						= maximum loglikelihood value
// logZ							= log evidence value
// logZerr						= error on log evidence value
// context						void pointer, any additional information

/*void dumper(int &nSamples, int &nlive, int &nPar, double **physLive, double **posterior, double **paramConstr, double &maxLogLike, double &logZ, double &logZerr, void *context)
{
	// convert the 2D Fortran arrays to C++ arrays
	
	
	// the posterior distribution
	// postdist will have nPar parameters in the first nPar columns & loglike value & the posterior probability in the last two columns
	
	int i, j;
	
	double postdist[nSamples][nPar + 2];
	for( i = 0; i < nPar + 2; i++ )
		for( j = 0; j < nSamples; j++ )
			postdist[j][i] = posterior[0][i * nSamples + j];
	
	
	
	// last set of live points
	// pLivePts will have nPar parameters in the first nPar columns & loglike value in the last column
	
	double pLivePts[nlive][nPar + 1];
	for( i = 0; i < nPar + 1; i++ )
		for( j = 0; j < nlive; j++ )
			pLivePts[j][i] = physLive[0][i * nlive + j];
}*/

/***********************************************************************************************************************/




/************************************************* BAMBI routine ******************************************************/

// The BAMBI routine will be called every updInt iterations
// only the root node has the data
//
//
// Arguments:
//
// ndata 						= total number data points
// ndim 						= total number input arguments
// BAMBIData[1][ndata * (ndim + 1)] 			= 2D array points from the last ndata iterations. Each point has the parameter values in unit
//							hypercube & the corresponding logL value
// lowlike						= loglike value defining current current constraint

void bambi(int *ndatain, int *ndimin, double **BAMBIData, double *lowlikein)
{
	int ndata = *ndatain;
	int ndim = *ndimin;
	double lowlike = *lowlikein;
	
	if( ( useNN==1 || useNN==2 ) && !discardpts )
	{
		int n=loglcalls;
#ifdef PARALLEL
		if( myid != 0 )
		{
			MPI_Send(&n, 1, MPI_INT, 0, myid, MPI_COMM_WORLD);
		}
		else
		{
			MPI_Status status;
			for( int i=1;i<nproc;i++ )
			{
				int k;
				MPI_Recv(&k, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);
				n += k;
			}
		}
#endif
		//if( myid == 0 ) std::cout << "Number of calls to actual logL code: " << n << "\n";
		if( myid == 0 ) printf("Number of calls to actual logL code: %d\n",n);
	}
	
	if( doBAMBI==0 ) return;
	
	int ready = 1;	
	if( myid == 0 )
	{
		for( int j = 0; j < ndata; j++ )
		{
			if( BAMBIData[0][ndim*ndata+j] - lowlike > logLRange )
			{
				ready = 0;
				break;
			}
		}
	}
#ifdef PARALLEL
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Bcast(&ready, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
	if( ready == 0 ) return;
	
	// converged to false if any of the nodes have converged=false
	int cvg=converged?1:0;

#ifdef PARALLEL
	MPI_Barrier(MPI_COMM_WORLD);
	if( myid != 0 )
	{
		MPI_Send(&cvg, 1, MPI_INT, 0, myid, MPI_COMM_WORLD);
	}
	else
	{
		MPI_Status status;
		for( int i=1;i<nproc;i++ )
		{
			MPI_Recv(&cvg, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);
			if(cvg==0) converged=false;
		}
	}
	
	if(myid==0 && !converged) cvg=0;
	MPI_Bcast(&cvg, 1, MPI_INT, 0, MPI_COMM_WORLD);
	converged=cvg==1?true:false;
#endif
	
	if( converged ) return;
	
	// if the networks converged last time & re-training is required then collect a few more points before re-training
	if( !converged && lastconverged && ignoredbambicalls<2 )
	{
		ignoredbambicalls++;
		return;
	}
	
	lastconverged=false;
	ignoredbambicalls=0;
	int i;
	
	if( !converged )
	{
		// convert the 2D Fortran arrays to C++ arrays
		float **data = new float* [ndata];
		for( i=0 ; i<ndata ; i++ ) data[i] = new float [ndim+1];
		if( myid == 0 )
		{
			for( int i = 0; i < ndim+1; i++ )
				for( int j = 0; j < ndata; j++ )
					data[j][i] = (float)BAMBIData[0][i*ndata+j];
			
			double TCube[ndim];
			for( int i = 0; i < ndata; i++ )
			{
				for( int j = 0; j < ndim; j++ ) TCube[j] = (double)data[i][j];
				getphysparams(TCube,&ndim,&npars2,context2);
				for( int j = 0; j < ndim; j++ ) data[i][j] = (float)TCube[j];
			}
		}
		
		// copy the C data array to all the nodes
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		for( int j = 0; j < ndata; j++ ) MPI_Bcast(data[j], ndim+1, MPI_FLOAT, 0, MPI_COMM_WORLD);
#endif
		// network training ....
		bool train[ndata];
		if( myid==0 ) AddNewTrainData(root,ndata,ndim,data,0.8,train,3);
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
#endif
		//netres=false;
		TrainNetwork(networkinputs,root,root,&nlayers,nnodes,netres,false);
		
		// set to resume on following runs
		if( whitenin!=3 && whitenout!=3 ) netres = true;
		
		// Initialise network element if not done already
		if( !likenetinit )
		{
			if( myid==0 ) printf("Initialising network element\n");
			nn = new FeedForwardNeuralNetwork();
			likenetinit=true;
		}
		
		// Load network from file
		if( myid==0 ) printf("Loading network.\n");
		char netfile[100];
		float a,b,rate;
		strcpy(netfile,root);
		strcat(netfile,"network.txt");
		nn->read(netfile,&rate,&a,&b);
		
		// Load the output transform if hist equal transform is being used
		if( whitenout==3 )
		{
			char otransfile[100];
			strcpy(otransfile,root);
			strcat(otransfile,"otrans.txt");
			std::ifstream otranfin(otransfile);
			//FILE *otranfin=fopen(otransfile,"r");
			otrans.read(otranfin);
			otranfin.close();
			//fclose(otranfin);
		}
		
		// Test convergence
		if( myid==0 )
		{
			float netL;
			//std::vector <float> logLlist;
			float logLlist[ndata];
			converged=true;
			i=0;
			float sd[]={0.0,0.0},xbar[]={0.0,0.0},x2bar[]={0.0,0.0};
			int nd[]={0,0};
			while( i<ndata )
			{
				if( data[i][ndim]>=lowlike )
				{
					//logLlist.push_back(data[i][ndim]);
					logLlist[i]=data[i][ndim];
					nn->forwardOne(1,data[i],&netL);
					if( whitenout==3 ) otrans.inverse(&netL,&netL,1);
					int j=train[i]?0:1;
					xbar[j]+=data[i][ndim]-netL;
					x2bar[j]+=pow(data[i][ndim]-netL,2.0);
					nd[j]++;
				}
				i++;
			}
			sd[0]=sqrt(fmax(0.0,x2bar[0]/nd[0]-pow(xbar[0]/nd[0],2.0)));
			sd[1]=sqrt(fmax(0.0,x2bar[1]/nd[1]-pow(xbar[1]/nd[1],2.0)));
			if( sd[0]>tol || sd[1]>tol )
			{
				converged=false;
				printf("Found standard deviation on (train, test) set (%g, %g) greater than the tol %g.\n",sd[0],sd[1],tol);
			}
			
			if( converged )
			{
				//std::sort(logLlist.begin(),logLlist.end());
				//thL[2]=logLlist[logLlist.size()*99/100];
				//thL[0]=logLlist[0]-2.0*tol;
				//thL[1]=logLlist.back()+2.0*tol;
				quickSort(logLlist,ndata);
				thL[2]=logLlist[ndata*99/100];
				thL[0]=logLlist[0]-2.0*tol;
				thL[1]=logLlist[ndata-1]+2.0*tol;
				printf("Converged. Found standard deviation on (train, test) set (%g, %g) less than the tol %g.\n",sd[0],sd[1],tol);
			}
			
			cvg=converged?1:0;
		}
	
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Bcast(&cvg, 1, MPI_INT, 0, MPI_COMM_WORLD);
		if( cvg==1 ) MPI_Bcast(thL, 3, MPI_FLOAT, 0, MPI_COMM_WORLD);
#endif
		
		converged=cvg==1?true:false;
		lastconverged=converged;
		
		for( i=0 ; i<ndata ; i++ ) delete [] data[i];
		delete data;
		
		static int nbambicall = 0;
		if( myid==0 && converged )
		{
			nbambicall++;
			//std::string sourcefile, targetfile;
			char sourcefile[150],targetfile[150];
			
			{
				//std::stringstream ss;
				//sourcefile = std::string(root) + "network.txt";
				//ss << sourcefile << "." << nbambicall;
				//targetfile = ss.str();
				sprintf(sourcefile,"%snetwork.txt",root);
				sprintf(targetfile,"%s.%d",sourcefile,nbambicall);
				CopyFile(sourcefile, targetfile);
			}
			
			{
				//std::stringstream ss;
				//sourcefile = std::string(root) + "train_pred.txt";
				//ss << sourcefile << "." << nbambicall;
				//targetfile = ss.str();
				sprintf(sourcefile,"%strain_pred.txt",root);
				sprintf(targetfile,"%s.%d",sourcefile,nbambicall);
				CopyFile(sourcefile, targetfile);
			}
			
			{
				//std::stringstream ss;
				//sourcefile = std::string(root) + "train.txt";
				//ss << sourcefile << "." << nbambicall;
				//targetfile = ss.str();
				sprintf(sourcefile,"%strain.txt",root);
				sprintf(targetfile,"%s.%d",sourcefile,nbambicall);
				CopyFile(sourcefile, targetfile);
			}
			
			{
				//std::stringstream ss;
				//sourcefile = std::string(root) + "test_pred.txt";
				//ss << sourcefile << "." << nbambicall;
				//targetfile = ss.str();
				sprintf(sourcefile,"%stest_pred.txt",root);
				sprintf(targetfile,"%s.%d",sourcefile,nbambicall);
				CopyFile(sourcefile, targetfile);
			}
			
			{
				//std::stringstream ss;
				//sourcefile = std::string(root) + "test.txt";
				//ss << sourcefile << "." << nbambicall;
				//targetfile = ss.str();
				sprintf(sourcefile,"%stest.txt",root);
				sprintf(targetfile,"%s.%d",sourcefile,nbambicall);
				CopyFile(sourcefile, targetfile);
			}
			
			{
				//std::stringstream ss;
				//std::string likefile = std::string(root) + "lowlike.txt";
				//std::ofstream fout;
				//fout.open(likefile.c_str(), std::ios::out | std::ios::app);
				//fout << lowlike << "\n";
				//fout.close();
				char likefile[150];
				sprintf(likefile,"%slowlike.txt",root);
				FILE *fout=fopen(likefile,"w");
				fprintf(fout,"%f\n",lowlike);
				fclose(fout);
			}
		}
	
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
#endif
	}
}

/***********************************************************************************************************************/

/************************************************* First Run Check ****************************************************/

// This is called on the first likelihood call if resuming is enabled.
// Checks to see if a saved network exists and if it is converged.
// Loads the saved network if so.
//

void FirstRunCheck(int ndim)
{
	bool readacc = false;
	
	if( doBAMBI==1 )
	{
		bool classnet,prior,noise,wnoise,fixseed,evidence,histmaxent,recurrent,norbias,resetalpha,resetsigma;
		bool hhps,text,vdata,autoencoder,pretrain,indep;
		float frac,sigma,rate,ratemin,randweights,stdev;
		char dummy[50];
		int printfreq,fixedseed,maxniter,nin,nout,stopf,hhpl,useSD,verbose,nepoch=25,lnsrch;
		std::vector <size_t> nhid;
			
		ReadInputFile2(networkinputs,nhid,&classnet,&frac,&prior,&noise,&wnoise,&sigma,&rate,&printfreq,&fixseed,&fixedseed,
		&evidence,&histmaxent,&recurrent,&whitenin,&whitenout,&stopf,&hhps,&hhpl,&maxniter,&nin,&nout,&norbias,&useSD,
		&text,&vdata,dummy,&resetalpha,&resetsigma,&autoencoder,&pretrain,&nepoch,&indep,&ratemin,&logLRange,&randweights,
		&verbose,&readacc,&stdev,&lnsrch);
		
		int cvg=1;
		int i;
		char temp[1000];
		float stor,valt,valp;
		char netfile[100];
		strcpy(netfile,root);
		strcat(netfile,"network.txt");
		
		int n=0;
		if(myid==0)
		{
			for(;;)
			{
				//std::stringstream ss;
				//std::string filename = std::string(root) + "network.txt";
				//ss << filename << "." << n+1;
				//filename = ss.str();
				char filename[150];
				sprintf(filename,"%snetwork.txt.%d",root,n+1);
				
				//std::ifstream fin(filename.c_str());
				//if( fin.fail() ) break;
				//fin.close();
				if( FILE *fin=fopen(filename,"r") ) n++;
				else break;
				
				//n++;
			}
		}
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
		if( !noise && !wnoise ) tol=sigma;
		
		if(n==0) cvg=0;
		
		if(cvg==1 && myid==0)
		{
			char traintrue[100],trainpred[100],testtrue[100],testpred[100];
			strcpy(traintrue,root);
			strcpy(trainpred,root);
			strcpy(testtrue,root);
			strcpy(testpred,root);
			strcat(traintrue,"train.txt");
			strcat(trainpred,"train_pred.txt");
			strcat(testtrue,"test.txt");
			strcat(testpred,"test_pred.txt");
			
			FILE *in2=fopen(trainpred,"r");
			FILE *in4=fopen(testpred,"r");
			
			float dummy;
			std::vector <float> logLlist;
			//float logLlist[ndata];
			int ii=0;
			while( !feof(in2) )
			{
				for(i=0;i<ndim;i++) fscanf(in2,"%f\t",&dummy);
				fscanf(in2,"%f\t%f\t%f\n",&valt,&valp,&dummy);
				logLlist.push_back(valt);
				//logLlist[ii]=valt;
				ii++;
			}
			
			while( !feof(in4) )
			{
				for(i=0;i<ndim;i++) fscanf(in4,"%f\t",&dummy);
				fscanf(in4,"%f\t%f\t%f\n",&valt,&valp,&dummy);
				logLlist.push_back(valt);
				//logLlist[ii]=valt;
				ii++;
			}
			
			fclose(in2);
			fclose(in4);
			
			std::sort(logLlist.begin(),logLlist.end());
			thL[2]=logLlist[logLlist.size()*19/20];
			thL[0]=logLlist[0]-2.0*tol;
			thL[1]=logLlist.back()+2.0*tol;
			//quickSort(logLlist,ndata);
			//thL[2]=logLlist[ndata*19/20];
			//thL[0]=logLlist[0]-2.0*tol;
			//thL[1]=logLlist[ndata-1]+2.0*tol;
		}
		
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		if( cvg==1 ) MPI_Bcast(thL, 3, MPI_FLOAT, 0, MPI_COMM_WORLD);
#endif
		
		if( cvg==1 && whitenin!=3 && whitenout!=3 )
		{
			if( myid==0 ) printf("Stored network is converged.\n");
			
			if( myid==0 ) printf("Initialising network element\n");
			nn = new FeedForwardNeuralNetwork();
			likenetinit=true;
				
			if( myid==0 ) printf("Loading network.\n");
			float a,b,rate;
			nn->read(netfile,&rate,&a,&b);
		}
		
		converged=cvg==1?true:false;
		lastconverged=converged;
	}
	else if( useNN==1 )
	{
		ReadInputFile3(networkinputs,&discardpts);
		
		nNN=0;
		if( myid == 0 )
		{
			for(;;)
			{
				//std::stringstream ss;
				//std::string filename = std::string(root) + "network.txt";
				//ss << filename << "." << nNN+1;
				//filename = ss.str();
				char filename[150];
				sprintf(filename,"%snetwork.txt.%d",root,nNN+1);
				
				//std::ifstream fin(filename.c_str());
				//if( fin.fail() ) break;
				//fin.close();
				if( FILE *fin=fopen(filename,"r") ) nNN++;
				else break;
				
				//nNN++;
			}
			printf("Found %d networks.\n",nNN);
		}
		
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Bcast(&nNN, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
		
		//args.resize(nNN);
		args = (NN_args *)malloc(nNN*sizeof(NN_args));
		omicron = (float *)malloc(nNN*sizeof(float));
		maxsigma = (double *)malloc(nNN*sizeof(double));
		for( int ii=0; ii<nNN; ii++ ) maxsigma[ii]=0.;
		
		for(int n=0;n<nNN;n++)
		{
			//std::string filename;
			//std::stringstream ss1, ss2, ss3;
			char filename[150];
			float alpha;
			float beta, rate;
			
			args[n].prior=true;
			args[n].noise=false;
			args[n].nalphas=1;
			args[n].Bcode=true;
			
			//filename = std::string(root) + "network.txt";
			//ss1 << filename << "." << n+1;
			//filename=ss1.str();
			sprintf(filename,"%snetwork.txt.%d",root,n+1);
				
			NeuralNetwork *nn1=new FeedForwardNeuralNetwork();
			nn1->read(filename, &rate, &alpha, &beta);
				
			args[n].nn=new FeedForwardNeuralNetwork(*nn1);;
			args[n].np=args[n].nn->nweights;
			args[n].alphas=new float[1];
			args[n].alphas[0]=alpha;
				
			//filename = std::string(root) + "train.txt";
			//ss2 << filename << "." << n+1;
			//filename=ss2.str();
			sprintf(filename,"%strain.txt.%d",root,n+1);
				
			args[n].td=new TrainingData(filename, false, readacc);
			for( size_t i=0;i<args[n].td->cumoutsets[args[n].td->ndata]*args[n].td->nout;i++ ) args[n].td->acc[i]=beta;
				
			args[n].pd=new PredictedData(args[n].nn->totnnodes, args[n].nn->totrnodes, args[n].td->ndata, args[n].td->nin, args[n].td->nout, args[n].td->cuminsets[args[n].td->ndata], args[n].td->cumoutsets[args[n].td->ndata]);
			//double logL;
			//args[n].nn->logLike(*(args[n].wtd), *(args[n].pd), logL, true);
			
			float wt[args[n].nn->nweights],wtgrad[args[n].nn->nweights];
			args[n].nn->getweights(wt);
			//omicron.push_back(FindOmicron(wt,&args[n],rate));
			omicron[n] = FindOmicron(wt,&args[n],rate,wtgrad);
			
			/*filename = std::string(root) + "test_pred.txt";
			ss3 << filename << "." << n+1;
			filename=ss3.str();
			std::ifstream fin(filename.c_str());
			maxsigma.push_back(0.0);
			while(!fin.eof())
			{
				float dummy,s;
				for(int i=0;i<ndim+2;i++) fin >> dummy;
				fin >> s;
				if(s>maxsigma[n]) maxsigma[n]=s;
			}
			fin.close();*/
			sprintf(filename,"%strain_pred.txt.%d",root,n+1);
			FILE *fin=fopen(filename,"r");
			while( !feof(fin) )
			{
				float dummy,s;
				for(int i=0;i<ndim+2;i++) fscanf(fin,"%f",&dummy);
				fscanf(fin,"%f",&s);
				if( s>maxsigma[n] ) maxsigma[n]=s;
			}
			fclose(fin);
		}
		
		//if( myid==0 ) std::cout << "Loaded " << nNN << " Networks.\n";
		if( myid==0 ) printf("Loaded %d Networks.\n",nNN);
		
		if( nNN==0 ) useNN=0;
		
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
#endif
	}
	else if( useNN==2 )
	{
		ReadInputFile3(networkinputs,&discardpts);
		
		nNN=0;
		nNNerr=0;
		if( myid == 0 )
		{
			for(;;)
			{
				//std::stringstream ss;
				//std::string filename = std::string(root) + "network.txt";
				//ss << filename << "." << nNN+1;
				//filename = ss.str();
				char filename[150];
				sprintf(filename,"%snetwork.txt.%d",root,nNN+1);
				
				//std::ifstream fin(filename.c_str());
				//if( fin.fail() ) break;
				//fin.close();
				if( FILE *fin=fopen(filename,"r") ) nNN++;
				else break;
				
				//nNN++;
			}
			printf("Found %d networks.\n",nNN);
			
			if( nNN>0 )
			{
				for(;;)
				{
					//std::stringstream ss;
					//std::string filename = std::string(root) + "network.txt";
					//ss << filename << "." << nNN << "." << nNNerr+1;
					//filename = ss.str();
					char filename[150];
					sprintf(filename,"%snetwork.txt.%d.%d",root,nNN,nNNerr+1);
					
					//std::ifstream fin(filename.c_str());
					//if( fin.fail() ) break;
					//fin.close();
					if( FILE *fin=fopen(filename,"r") ) nNNerr++;
					else break;
					
					//nNNerr++;
				}
			}
			printf("Using %d networks each for error estimation.\n",nNNerr);
		}
		
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Bcast(&nNN, 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Bcast(&nNNerr, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif
		//maxsigma.resize(nNN);
		maxsigma=(double *)malloc(nNN*sizeof(double));
		
		nnstore = new NeuralNetwork** [nNN];
		for( int i=0; i<nNN; i++)
		{
			nnstore[i] = new NeuralNetwork* [nNNerr];
			
			//std::stringstream ss;
			//std::string filename;
			
			//filename = std::string(root) + "test.txt";
			//ss << filename << "." << i+1;
			//filename = ss.str();
			char filename[150];
			sprintf(filename,"%stest.txt.%d",root,i+1);
			
			TrainingData td(filename,false,readacc);
			
			for( int j=0; j<nNNerr; j++)
			{
				float rate,alpha,beta;
				
				//std::stringstream ss;
				//std::string filename = std::string(root) + "network.txt";
				//ss << filename << "." << i+1 << "." << j+1;
				//filename = ss.str();
				char filename[150];
				sprintf(filename,"%snetwork.txt.%d.%d",root,i+1,j+1);
				
				nnstore[i][j] = new FeedForwardNeuralNetwork();
				nnstore[i][j]->read(filename, &rate, &alpha, &beta);
			}
			
			double predouts[td.ndata][2];
			float output;
			for( int j=0; j<td.ndata; j++) predouts[j][0]=predouts[j][1]=0.0;
			for( int j=0; j<nNNerr; j++)
			{
				for( int n=0; n<td.ndata; n++){
					nnstore[i][j]->forwardOne(1,&td.inputs[n*ndim],&output);
					predouts[n][0]+=(double)output;
					predouts[n][1]+=(double)output*(double)output;
				}
			}
			
			for( int j=0; j<td.ndata; j++)
			{
				predouts[j][0]/=(double)nNNerr;
				predouts[j][1]=sqrt(predouts[j][1]/(double)nNNerr-predouts[j][0]*predouts[j][0]);
			}
			
			maxsigma[i]=0.0;
			for( int j=0; j<td.ndata; j++)
				if (predouts[j][1]>maxsigma[i]) maxsigma[i]=predouts[j][1];
		}
		
		if( nNN==0 || nNNerr==0) useNN=0;
#ifdef PARALLEL
		MPI_Barrier(MPI_COMM_WORLD);
#endif
	}
}

/***********************************************************************************************************************/

void GetNetTol()
{
	bool noise,wnoise;
	float sigma;
		
	ReadInputFile4(networkinputs,&noise,&wnoise,&sigma,&logLRange);
	
	if( !noise && !wnoise ) tol=sigma;
}

/*void CopyFile(std::string sourcefile, std::string targetfile)
{
	std::ifstream fin(sourcefile.c_str());
	std::ofstream fout(targetfile.c_str());
	for(;;)
	{
		if( fin.eof() ) break;
		std::string line;
		getline(fin,line);
		fout << line << "\n";
	}
	fin.close();
	fout.close();
}*/

void CopyFile(char *sourcefile, char *targetfile)
{
	FILE *fin=fopen(sourcefile,"r");
	FILE *fout=fopen(targetfile,"w");
	char ch;
	
	while( !feof(fin) )
	{
		ch = fgetc(fin);
		if( !feof(fin) ) fputc(ch,fout);
	}
	
	fclose(fin);
	fclose(fout);
}

void quickSort(float numbers[], int array_size)
{
  q_sort(numbers, 0, array_size - 1);
}
 
 
void q_sort(float numbers[], int left, int right)
{
  float pivot;
  int l_hold, r_hold;
 
  l_hold = left;
  r_hold = right;
  pivot = numbers[left];
  while (left < right)
  {
    while ((numbers[right] >= pivot) && (left < right))
      right--;
    if (left != right)
    {
      numbers[left] = numbers[right];
      left++;
    }
    while ((numbers[left] <= pivot) && (left < right))
      left++;
    if (left != right)
    {
      numbers[right] = numbers[left];
      right--;
    }
  }
  numbers[left] = pivot;
  pivot = left;
  left = l_hold;
  right = r_hold;
  if (left < pivot)
    q_sort(numbers, left, pivot-1);
  if (right > pivot)
    q_sort(numbers, pivot+1, right);
}

