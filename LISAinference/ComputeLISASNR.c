#include "ComputeLISASNR.h"

/************ Parsing arguments function ************/

/* Parse command line to initialize GenTDITDparams object */
/* Masses are input in solar masses and distances in Mpc - converted in SI for the internals */
static void parse_args_ComputeLISASNR(ssize_t argc, char **argv, ComputeLISASNRparams* params)
{
  char help[] = "\
GenerateWaveform by Sylvain Marsat, John Baker, and Philip Graff\n\
Copyright July 2015\n\
\n\
This program computes and prints the SNR for a LISA TDI waveform (currently only AET(XYZ) available); it will either:\n\
(i) generate a EOBNRv2HMROM waveform, process it through the Fourier domain response, and compute SNR using accelerated Fresnel overlap, with rescaling of the noise (follows LISAinference internals)\n\
(ii) generate a EOBNRv2HMROM waveform, process it through the Fourier domain response, and compute SNR using ordinary linear overlap, with rescaling of the noise (follows LISAinference internals)\n\
(iii) load as input time series for 3 TDI AET observables, FFT them, and compute SNR with linear integration, without rescaling of the noise.\n\
Arguments are as follows:\n\
\n\
--------------------------------------------------\n\
----- Physical Parameters ------------------------\n\
--------------------------------------------------\n\
 --tRef                Time at reference frequency (sec, default=0)\n\
 --phiRef              Orbital phase at reference frequency (radians, default=0)\n\
 --fRef                Reference frequency (Hz, default=0, interpreted as Mf=0.14)\n\
 --m1                  Component mass 1 in Solar masses (larger, default=2e6)\n\
 --m2                  Component mass 2 in Solar masses (smaller, default=1e6)\n\
 --distance            Distance to source in Mpc (default=1e3)\n\
 --inclination         Inclination of source orbital plane to observer line of sight\n\
                       (radians, default=PI/3)\n\
 --lambda              First angle for the position in the sky (radians, default=0)\n\
 --beta                Second angle for the position in the sky (radians, default=0)\n\
 --polarization        Polarization of source (radians, default=0)\n\
\n\
--------------------------------------------------\n\
----- Generation Parameters ----------------------\n\
--------------------------------------------------\n\
 --nbmode              Number of modes of radiation to generate (1-5, default=5)\n\
 --fLow                Minimal frequency (Hz, default=0) - when too low, use first frequency covered by the ROM\n\
 --tagtdi              Tag choosing the set of TDI variables to use (default TDIAETXYZ)\n\
 --tagint              Tag choosing the integrator: 0 for Fresnel (default), 1 for linear integration\n\
 --nbptsoverlap        Number of points to use for linear integration (default 32768)\n\
 --fromtditdfile       Option for loading time series for TDI observables and FFTing (default: false)\n\
 --nlinesinfile        Number of lines of inputs file when loading TDI time series from file\n\
 --indir               Input directory when loading TDI time series from file\n\
 --infile              Input file name when loading TDI time series from file\n\
\n";

  ssize_t i;

  /* Set default values for the physical params */
  params->tRef = 0.;
  params->phiRef = 0.;
  params->fRef = 0.;
  params->m1 = 2*1e6;
  params->m2 = 1*1e6;
  params->distance = 1e3;
  params->inclination = PI/3;
  params->lambda = 0;
  params->beta = 0;
  params->polarization = 0;

  /* Set default values for the generation params */
  params->nbmode = 5;
  params->fLow = __LISASimFD_Noise_fLow;
  params->tagtdi = TDIAETXYZ;
  params->tagint = 0;
  params->nbptsoverlap = 32768;
  params->fromtditdfile = 0;
  params->nlinesinfile = 0;    /* No default; has to be provided */
  strcpy(params->indir, "");   /* No default; has to be provided */
  strcpy(params->infile, "");  /* No default; has to be provided */

  /* Consume command line */
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--help") == 0) {
      fprintf(stdout,"%s", help);
      exit(0);
    } else if (strcmp(argv[i], "--tRef") == 0) {
      params->tRef = atof(argv[++i]);
    } else if (strcmp(argv[i], "--phiRef") == 0) {
      params->phiRef = atof(argv[++i]);
    } else if (strcmp(argv[i], "--fRef") == 0) {
      params->fRef = atof(argv[++i]);
    } else if (strcmp(argv[i], "--m1") == 0) {
      params->m1 = atof(argv[++i]);
    } else if (strcmp(argv[i], "--m2") == 0) {
      params->m2 = atof(argv[++i]);
    } else if (strcmp(argv[i], "--distance") == 0) {
      params->distance = atof(argv[++i]);
    } else if (strcmp(argv[i], "--inclination") == 0) {
      params->inclination = atof(argv[++i]);
    } else if (strcmp(argv[i], "--lambda") == 0) {
      params->lambda = atof(argv[++i]);
    } else if (strcmp(argv[i], "--beta") == 0) {
      params->beta = atof(argv[++i]);
    } else if (strcmp(argv[i], "--polarization") == 0) {
      params->polarization = atof(argv[++i]);
    } else if (strcmp(argv[i], "--nbmode") == 0) {
      params->nbmode = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--fLow") == 0) {
      params->fLow = atof(argv[++i]);
    } else if (strcmp(argv[i], "--tagtdi") == 0) {
      params->tagtdi = ParseTDItag(argv[++i]);
    } else if (strcmp(argv[i], "--tagint") == 0) {
      params->tagint = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--nbptsoverlap") == 0) {
      params->nbptsoverlap = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--fromtditdfile") == 0) {
      params->fromtditdfile = 1;
    } else if (strcmp(argv[i], "--nlinesinfile") == 0) {
      params->nlinesinfile = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--indir") == 0) {
      strcpy(params->indir, argv[++i]);
    } else if (strcmp(argv[i], "--infile") == 0) {
      strcpy(params->infile, argv[++i]);
    } else {
      printf("Error: invalid option: %s\n", argv[i]);
      printf("argc-i=%i\n",argc-i); 
      goto fail;
    }
  }

  return;

 fail:
  exit(1);
}

/************ Functions to write waveforms to file ************/

/* Read waveform time series in Re/Im form for hpTD and hcTD a single file */
/* NOTE: assumes the same number of points is used to represent each mode */
static void Read_Text_TDITD3Chan( RealTimeSeries** TDI1, RealTimeSeries** TDI2, RealTimeSeries** TDI3, const char dir[], const char file[], const int nblines)
{
  /* Initalize and read input */
  gsl_matrix* inmatrix =  gsl_matrix_alloc(nblines, 4);
  Read_Text_Matrix(dir, file, inmatrix);

  /* Initialize structures */
  RealTimeSeries_Init(TDI1, nblines);
  RealTimeSeries_Init(TDI2, nblines);
  RealTimeSeries_Init(TDI3, nblines);

  /* Set values */
  gsl_vector_view timesview = gsl_matrix_column(inmatrix, 0);
  gsl_vector_view TDI1view = gsl_matrix_column(inmatrix, 1);
  gsl_vector_view TDI2view = gsl_matrix_column(inmatrix, 2);
  gsl_vector_view TDI3view = gsl_matrix_column(inmatrix, 3);
  gsl_vector_memcpy((*TDI1)->times, &timesview.vector);
  gsl_vector_memcpy((*TDI2)->times, &timesview.vector);
  gsl_vector_memcpy((*TDI3)->times, &timesview.vector);
  gsl_vector_memcpy((*TDI1)->h, &TDI1view.vector);
  gsl_vector_memcpy((*TDI2)->h, &TDI2view.vector);
  gsl_vector_memcpy((*TDI3)->h, &TDI3view.vector);

  /* Clean up */
  gsl_matrix_free(inmatrix);
}

/***************** Main program *****************/

int main(int argc, char *argv[])
{
  double SNR = 0;

  /* Initialize structure for parameters */
  ComputeLISASNRparams* params;
  params = (ComputeLISASNRparams*) malloc(sizeof(ComputeLISASNRparams));
  memset(params, 0, sizeof(ComputeLISASNRparams));

  /* Parse commandline to read parameters */
  parse_args_ComputeLISASNR(argc, argv, params);

  /* NOTE: supports only AET(XYZ) (orthogonal) */
  if(!(params->tagtdi==TDIAETXYZ)) {
    printf("Error in ComputeLISASNR: TDI tag not recognized.\n");
    exit(1);
  }

  else {

    if(params->fromtditdfile) {
      /* Load TD TDI from file */
      RealTimeSeries* TDI1 = NULL;
      RealTimeSeries* TDI2 = NULL;
      RealTimeSeries* TDI3 = NULL;
      Read_Text_TDITD3Chan(&TDI1, &TDI2, &TDI3, params->indir, params->infile, params->nlinesinfile);

      /* Compute FFT */
      ReImFrequencySeries* TDI1FFT = NULL;
      ReImFrequencySeries* TDI2FFT = NULL;
      ReImFrequencySeries* TDI3FFT = NULL;
      gsl_vector* times = TDI1->times;
      double twindowbeg = 0.05 * (gsl_vector_get(times, times->size - 1) - gsl_vector_get(times, 0)); /* Here hardcoded relative window lengths */
      double twindowend = 0.01 * (gsl_vector_get(times, times->size - 1) - gsl_vector_get(times, 0)); /* Here hardcoded relative window lengths */
      FFTTimeSeries(&TDI1FFT, TDI1, twindowbeg, twindowend, 2); /* Here hardcoded 0-padding */
      FFTTimeSeries(&TDI2FFT, TDI2, twindowbeg, twindowend, 2); /* Here hardcoded 0-padding */
      FFTTimeSeries(&TDI3FFT, TDI3, twindowbeg, twindowend, 2); /* Here hardcoded 0-padding */

      /* Restrict FFT on frequency interval of interest - no limitation put on the upper bound */
      ReImFrequencySeries* TDI1FFTrestr = NULL;
      ReImFrequencySeries* TDI2FFTrestr = NULL;
      ReImFrequencySeries* TDI3FFTrestr = NULL;
      RestrictFDReImFrequencySeries(&TDI1FFTrestr, TDI1FFT, params->fLow, __LISASimFD_Noise_fHigh);
      RestrictFDReImFrequencySeries(&TDI2FFTrestr, TDI2FFT, params->fLow, __LISASimFD_Noise_fHigh);
      RestrictFDReImFrequencySeries(&TDI3FFTrestr, TDI3FFT, params->fLow, __LISASimFD_Noise_fHigh);

      /* Compute SNR with linear integration, weighting with non-rescaled noise functions */
      /* Note: assumes same lengths for all FD FFT frequency seriess */
      /* Note: not-rescaled noise functions */
      int sizeA = TDI1FFTrestr->freq->size;
      int sizeE = TDI2FFTrestr->freq->size;
      int sizeT = TDI3FFTrestr->freq->size;
      gsl_vector* noisevaluesA = gsl_vector_alloc(sizeA);
      gsl_vector* noisevaluesE = gsl_vector_alloc(sizeE);
      gsl_vector* noisevaluesT = gsl_vector_alloc(sizeT);
      for(int i=0; i<sizeA; i++) {
	gsl_vector_set(noisevaluesA, i, SnAXYZNoRescaling(gsl_vector_get(TDI1FFTrestr->freq, i)));
      }
      for(int i=0; i<sizeE; i++) {
	gsl_vector_set(noisevaluesE, i, SnEXYZNoRescaling(gsl_vector_get(TDI2FFTrestr->freq, i)));
      }
      for(int i=0; i<sizeT; i++) {
	gsl_vector_set(noisevaluesT, i, SnTXYZNoRescaling(gsl_vector_get(TDI3FFTrestr->freq, i)));
      }
      double SNRA2 = FDOverlapReImvsReIm(TDI1FFTrestr, TDI1FFTrestr, noisevaluesA);
      double SNRE2 = FDOverlapReImvsReIm(TDI2FFTrestr, TDI2FFTrestr, noisevaluesE);
      double SNRT2 = FDOverlapReImvsReIm(TDI3FFTrestr, TDI3FFTrestr, noisevaluesT);
      SNR = sqrt(SNRA2 + SNRE2 + SNRT2);
    }

    else {
      /* Initialize the structure for LISAparams and GlobalParams and copy values */
      /* NOTE: injectedparams and globalparams are declared as extern in LISAutils.h, and used by generation functions */
      injectedparams = (LISAParams*) malloc(sizeof(LISAParams));
      memset(injectedparams, 0, sizeof(LISAParams));
      injectedparams->tRef = params->tRef;
      injectedparams->phiRef = params->phiRef;
      injectedparams->m1 = params->m1;
      injectedparams->m2 = params->m2;
      injectedparams->distance = params->distance;
      injectedparams->lambda = params->lambda;
      injectedparams->beta = params->beta;
      injectedparams->inclination = params->inclination;
      injectedparams->polarization = params->polarization;
      injectedparams->nbmode = params->nbmode;
      globalparams = (LISAGlobalParams*) malloc(sizeof(LISAGlobalParams));
      memset(globalparams, 0, sizeof(LISAGlobalParams));
      globalparams->fRef = params->fRef;
      globalparams->deltatobs = 1.; /* Default value */
      globalparams->minf = params->fLow;
      globalparams->nbmodeinj = params->nbmode;
      globalparams->nbmodetemp = params->nbmode;
      globalparams->tagint = params->tagint;
      globalparams->tagtdi = params->tagtdi;
      globalparams->nbptsoverlap = params->nbptsoverlap;

      /* Set geometric coefficients */
      SetCoeffsG(params->lambda, params->beta, params->polarization);

      /* Branch between the Fresnel or linear computation */
      if(params->tagint==0) {
	LISAInjectionCAmpPhase* injCAmpPhase = NULL;
	LISAInjectionCAmpPhase_Init(&injCAmpPhase);
	LISAGenerateInjectionCAmpPhase(injectedparams, injCAmpPhase);
	SNR = sqrt(injCAmpPhase->TDI123ss);
      }
      else if(params->tagint==1) {
	LISAInjectionReIm* injReIm = NULL;
	LISAInjectionReIm_Init(&injReIm);
	LISAGenerateInjectionReIm(injectedparams, params->fLow, params->nbptsoverlap, 0, injReIm); /* Hardcoded linear sampling */

	double SNRA2 = FDOverlapReImvsReIm(injReIm->TDI1Signal, injReIm->TDI1Signal, injReIm->noisevalues1);
	double SNRE2 = FDOverlapReImvsReIm(injReIm->TDI2Signal, injReIm->TDI2Signal, injReIm->noisevalues2);
	double SNRT2 = FDOverlapReImvsReIm(injReIm->TDI3Signal, injReIm->TDI3Signal, injReIm->noisevalues3);
	SNR = sqrt(SNRA2 + SNRE2 + SNRT2);
      }
      else {
	printf("Error in ComputeLISASNR: integration tag not recognized.\n");
	exit(1);
      }
    }

    /* Print SNR to stdout */
    printf("%.8f\n", SNR);
  }
}