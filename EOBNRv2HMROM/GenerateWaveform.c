#include "GenerateWaveform.h"

/************ Parsing arguments function ************/

/* Function to convert string input TDI string to TDItag */
static GenWavetag ParseGenWavetag(char* string) {
  GenWavetag tag;
  if(strcmp(string, "hlm")==0) tag = hlm;
  else if(strcmp(string, "h22TD")==0) tag = h22TD;
  else if(strcmp(string, "hphcFD")==0) tag = hphcFD;
  else if(strcmp(string, "hphcTD")==0) tag = hphcTD;
  else {
    printf("Error in ParseGenWavetag: string not recognized.\n");
    exit(1);
  }
  return tag;
}

/* Parse command line to initialize GenWaveParams object */
/* Masses are input in solar masses and distances in Mpc - converted in SI for the internals */
static void parse_args_GenerateWaveform(ssize_t argc, char **argv, GenWaveParams* params)
{
    char help[] = "\
GenerateWaveform by Sylvain Marsat, John Baker, and Philip Graff\n\
Copyright July 2015\n\
\n\
This program generates and outputs a waveform produced with EOBNRv2HMROM.\n\
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
\n\
--------------------------------------------------\n\
----- Generation Parameters ----------------------\n\
--------------------------------------------------\n\
 --nbmode              Number of modes of radiation to generate (1-5, default=5)\n\
 --minf                Minimal frequency, ignore if 0 (Hz, default=0) - will use first frequency covered by the ROM if higher\n\
 --maxf                Maximal frequency, ignore if 0 (Hz, default=0) - will use last frequency covered by the ROM if lower\n\
 --deltatobs           Observation duration (years, default=2)\n\
 --tagextpn            Tag to allow PN extension of the waveform at low frequencies (default=1)\n\
 --Mfmatch             When PN extension allowed, geometric matching frequency: will use ROM above this value. If <=0, use ROM down to the lowest covered frequency (default=0.)\n\
 --taggenwave          Tag choosing the wf format: hlm (default: downsampled modes, Amp/Phase form),  h22TD (IFFT of h22, Amp/Phase form - currently not supported for higher modes), hphcFD (hlm interpolated and summed, Re/Im form), hphcTD (IFFT of hphcFD, Re/Im form)\n\
 --f1windowend         If generating h22TD/hphcTD, start frequency for windowing at the end - set to 0 to ignore and use 0.995*fHighROM, where fHighROM is the highest frequency covered by the ROM (Hz, default=0)\n\
 --f2windowend         If generating h22TD/hphcTD, stop frequency for windowing at the end - set to 0 to ignore and use fHighROM, where fHighROM is the highest frequency covered by the ROM (Hz, default=0)\n\
 --binaryout           Tag for outputting the data in gsl binary form instead of text (default false)\n\
 --outdir              Output directory\n\
 --outfile             Output file name\n\
\n";

    ssize_t i;

    /* set default values for the physical params */
    params->tRef = 0.;
    params->phiRef = 0.;
    params->fRef = 0.;
    params->m1 = 2*1e6;
    params->m2 = 1*1e6;
    params->distance = 1e3;
    params->inclination = PI/3;

    /* set default values for the generation params */
    params->nbmode = 5;
    params->minf = 0.;
    params->maxf = 0.;
    params->deltatobs = 2.;
    params->tagextpn = 1;
    params->Mfmatch = 0.;
    params->taggenwave = hlm;
    params->f1windowend = 0.;
    params->f2windowend = 0.;
    params->binaryout = 0;
    strcpy(params->outdir, ".");
    strcpy(params->outfile, "generated_waveform.txt");

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
        } else if (strcmp(argv[i], "--nbmode") == 0) {
          params->nbmode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--minf") == 0) {
          params->minf = atof(argv[++i]);
        } else if (strcmp(argv[i], "--maxf") == 0) {
          params->maxf = atof(argv[++i]);
        } else if (strcmp(argv[i], "--deltatobs") == 0) {
          params->deltatobs = atof(argv[++i]);
        } else if (strcmp(argv[i], "--tagextpn") == 0) {
          params->tagextpn = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--Mfmatch") == 0) {
          params->Mfmatch = atof(argv[++i]);
        } else if (strcmp(argv[i], "--taggenwave") == 0) {
          params->taggenwave = ParseGenWavetag(argv[++i]);
        } else if (strcmp(argv[i], "--f1windowend") == 0) {
          params->f1windowend = atof(argv[++i]);
        } else if (strcmp(argv[i], "--f2windowend") == 0) {
          params->f2windowend = atof(argv[++i]);
        } else if (strcmp(argv[i], "--binaryout") == 0) {
          params->binaryout = 1;
        } else if (strcmp(argv[i], "--outdir") == 0) {
            strcpy(params->outdir, argv[++i]);
        } else if (strcmp(argv[i], "--outfile") == 0) {
            strcpy(params->outfile, argv[++i]);
        } else {
	  printf("Error: invalid option: %s\n", argv[i]);
	  goto fail;
        }
    }

    return;

 fail:
    exit(1);
}

/************ Functions to write waveforms to file - text or binary ************/

/* Output waveform in downsampled form, FD Amp/Pase, all hlm modes in a single file */
/* NOTE: first version assumed the same number of points is used to represent each mode */
/* HACK: if not same number of freqs for the modes (due to PN extension), 0-pad at the end */
static void Write_Wave_hlm(const char dir[], const char file[], ListmodesCAmpPhaseFrequencySeries* listhlm, int nbmodes, int binary)
{
  /* Initialize output */
  /* get length from 22 mode - NOTE: assumes the same for all modes */
  int nbfreq = ListmodesCAmpPhaseFrequencySeries_GetMode(listhlm, 2, 2)->freqseries->freq->size;
  /* HACK: get the maximal length */
  for(int i=0; i<nbmodes; i++) {
    nbfreq = max(nbfreq, ListmodesCAmpPhaseFrequencySeries_GetMode(listhlm, listmode[i][0], listmode[i][1])->freqseries->freq->size);
  }
  gsl_matrix* outmatrix = gsl_matrix_alloc(nbfreq, 3*nbmodes);

  /* Get data in the list of modes */
  CAmpPhaseFrequencySeries* mode;
  for(int i=0; i<nbmodes; i++) {
    mode = ListmodesCAmpPhaseFrequencySeries_GetMode(listhlm, listmode[i][0], listmode[i][1])->freqseries;
    int nbfreqmode = mode->freq->size;
    for(int j=0; j<nbfreqmode; j++) {
      gsl_matrix_set(outmatrix, j, 0+3*i, gsl_vector_get(mode->freq, j));
      gsl_matrix_set(outmatrix, j, 1+3*i, gsl_vector_get(mode->amp_real, j)); /* amp_imag is 0 at this stage, we ignore it */
      gsl_matrix_set(outmatrix, j, 2+3*i, gsl_vector_get(mode->phase, j));
    }
    for(int j=nbfreqmode; j<nbfreq; j++) {
      gsl_matrix_set(outmatrix, j, 0+3*i, 0);
      gsl_matrix_set(outmatrix, j, 1+3*i, 0);
      gsl_matrix_set(outmatrix, j, 2+3*i, 0);
    }
  }

  /* Output */
  if (!binary) Write_Text_Matrix(dir, file, outmatrix);
  else Write_Matrix(dir, file, outmatrix);
}
/* Output waveform in frequency series form,Re/Im for hplus and hcross */
static void Write_Wave_hphcFD(const char dir[], const char file[], ReImFrequencySeries* hptilde, ReImFrequencySeries* hctilde, int binary)
{
  /* Initialize output */
  /* Note: assumes hplus, hcross have same length as expected */
  int nbfreq = hptilde->freq->size;
  gsl_matrix* outmatrix = gsl_matrix_alloc(nbfreq, 5);

  /* Set output matrix */
  gsl_matrix_set_col(outmatrix, 0, hptilde->freq);
  gsl_matrix_set_col(outmatrix, 1, hptilde->h_real);
  gsl_matrix_set_col(outmatrix, 2, hptilde->h_imag);
  gsl_matrix_set_col(outmatrix, 3, hctilde->h_real);
  gsl_matrix_set_col(outmatrix, 4, hctilde->h_imag);

  /* Output */
  if (!binary) Write_Text_Matrix(dir, file, outmatrix);
  else Write_Matrix(dir, file, outmatrix);
}
/* Output waveform in frequency series form,Re/Im for hplus and hcross */
static void Write_Wave_hphcTD(const char dir[], const char file[], RealTimeSeries* hp, RealTimeSeries* hc, int binary)
{
  /* Initialize output */
  /* Note: assumes hplus, hcross have same length as expected */
  int nbtimes = hp->times->size;
  gsl_matrix* outmatrix = gsl_matrix_alloc(nbtimes, 3);

  /* Set output matrix */
  gsl_matrix_set_col(outmatrix, 0, hp->times);
  gsl_matrix_set_col(outmatrix, 1, hp->h);
  gsl_matrix_set_col(outmatrix, 2, hc->h);

  /* Output */
  if (!binary) Write_Text_Matrix(dir, file, outmatrix);
  else Write_Matrix(dir, file, outmatrix);
}
/* Output waveform in frequency series form,Re/Im for hplus and hcross */
static void Write_Wave_h22TD(const char dir[], const char file[], ReImTimeSeries* h22, int binary)
{
  /* Initialize output */
  int nbtimes = h22->times->size;
  gsl_matrix* outmatrix = gsl_matrix_alloc(nbtimes, 3);

  /* Set output matrix */
  gsl_matrix_set_col(outmatrix, 0, h22->times);
  gsl_matrix_set_col(outmatrix, 1, h22->h_real);
  gsl_matrix_set_col(outmatrix, 2, h22->h_imag);

  /* Output */
  if (!binary) Write_Text_Matrix(dir, file, outmatrix);
  else Write_Matrix(dir, file, outmatrix);
}

/***************** Main program *****************/

int main(int argc, char *argv[])
{
  int ret;
  /* Initialize structure for parameters */
  GenWaveParams* params;
  params = (GenWaveParams*) malloc(sizeof(GenWaveParams));
  memset(params, 0, sizeof(GenWaveParams));

  /* Parse commandline to read parameters */
  parse_args_GenerateWaveform(argc, argv, params);

  /* Starting frequency - takes into account the duration of observation deltatobs and the arg minf */
  double fstartobs = 0.;
  if(!(params->deltatobs==0.)) fstartobs = Newtonianfoft(params->m1, params->m2, params->deltatobs);
  double fLow = fmax(params->minf, fstartobs);

  /* Generate Fourier-domain waveform as a list of hlm modes */
  /* Use TF2 extension, if required to, to arbitrarily low frequencies */
  /* NOTE: at this stage, if no extension is performed, deltatobs and minf are ignored - will start at MfROM*/
  ListmodesCAmpPhaseFrequencySeries* listROM = NULL;
  if(!(params->tagextpn)){
    //printf("Not Extending signal waveform.  mfmatch=%g\n",globalparams->mfmatch);
    ret = SimEOBNRv2HMROM(&listROM, params->nbmode, params->tRef, params->phiRef, params->fRef, (params->m1)*MSUN_SI, (params->m2)*MSUN_SI, (params->distance)*1e6*PC_SI);
  } else {
    //printf("Extending signal waveform.  mfmatch=%g\n",globalparams->mfmatch);
    ret = SimEOBNRv2HMROMExtTF2(&listROM, params->nbmode, params->Mfmatch, fLow, 0, params->tRef, params->phiRef, params->fRef, (params->m1)*MSUN_SI, (params->m2)*MSUN_SI, (params->distance)*1e6*PC_SI);
  }

  if(params->taggenwave==hlm) {
    Write_Wave_hlm(params->outdir, params->outfile, listROM, params->nbmode, params->binaryout);
    exit(0);
  }

  else if((params->taggenwave==hphcFD) || (params->taggenwave==hphcTD)) {

    /* Determine deltaf so that N deltat = 1/deltaf > 2*tc where tc is the time to coalescence estimated from Psi22 */
    /* NOTE: assumes the TD waveform ends around t=0 */
    /* NOTE: estimate based on the 22 mode - fstartobs will be scaled from mode to mode to ensure the same deltatobs for all (except for the 21 mode, which will turn on after the others) */
    double tc = EstimateInitialTime(listROM, fLow);

    double deltaf = 0.5 * 1./(2*(-tc)); /* Extra factor of 1/2 corresponding to 0-padding in TD by factor of 2 */

    /* Compute hplus, hcross FD */
    /* NOTE: we use fLow in the role of fstartobs - even when not asking for a deltatobs (deltatobs=0, fstartobs=0), the different modes will start at a frequency that correspond to the same starting time (again, except for the 21 mode which will turn on after the start of the waveform) */
    ReImFrequencySeries* hptilde = NULL;
    ReImFrequencySeries* hctilde = NULL;
    GeneratehphcFDReImFrequencySeries(&hptilde, &hctilde, listROM, params->minf, params->maxf, fLow, deltaf, 0, params->nbmode, params->inclination, 0., 1);

    /* Output */
    if(params->taggenwave==hphcFD) {

      Write_Wave_hphcFD(params->outdir, params->outfile, hptilde, hctilde, params->binaryout);
      exit(0);
    }

    else {

      /* Determine frequency windows - min,max frequencies determined directly from the Amp/Phase hlm's */
      /* The ROM output has possibly been PN-extended */
      double fLowROM = ListmodesCAmpPhaseFrequencySeries_minf(listROM);
      double fHighROM = ListmodesCAmpPhaseFrequencySeries_maxf(listROM);
      double f1windowbeg = fmax(fLow, fLowROM);
      double f2windowbeg = 1.1 * f1windowbeg; /* Here hardcoded relative width of the window */
      double f2windowend = fHighROM;
      double f1windowend = 0.995 * f2windowend; /* Here hardcoded relative width of the window */

      /* Compute hplus, hcross TD */
      RealTimeSeries* hp = NULL;
      RealTimeSeries* hc = NULL;
      IFFTFrequencySeriesReal(&hp, hptilde, f1windowbeg, f2windowbeg, f1windowend, f2windowend, 3); /* Here hardcoded nzeropadding */
      IFFTFrequencySeriesReal(&hc, hctilde, f1windowbeg, f2windowbeg, f1windowend, f2windowend, 3); /* Here hardcoded nzeropadding */

      /* Output */
      if(params->taggenwave==hphcTD) {
        Write_Wave_hphcTD(params->outdir, params->outfile, hp, hc, params->binaryout);
        exit(0);
      }
    }
  }
  else if(params->taggenwave==h22TD) {

    /* Determine deltaf so that N deltat = 1/deltaf > 2*tc where tc is the time to coalescence estimated from Psi22 */
    /* NOTE: assumes the TD waveform ends around t=0 */
    /* NOTE: estimate based on the 22 mode - see comments above for hphcFD when higher modes are included */
    double tc = EstimateInitialTime(listROM, fLow);

    double deltaf = 0.5 * 1./(2*(-tc)); /* Extra factor of 1/2 corresponding to 0-padding in TD by factor of 2 */

    //
    printf("tc, deltaf: %g, %g\n", tc, deltaf);

    /* Compute h22 FD on frequency samples to prepare FFT */
    /* NOTE: for fstartobs, see comments above for hphcFD when higher modes are included */
    ReImFrequencySeries* h22tilde = NULL;
    GenerateFDReImFrequencySeriesSingleMode(&h22tilde, listROM, params->minf, params->maxf, fstartobs, deltaf, 0, 2, 2);

    //
    printf("length h22tilde: %d \n", h22tilde->freq->size);

    /* Determine frequency windows - min,max frequencies determined from the Amp/Phase h22 */
    /* The ROM output has possibly been PN-extended */
    CAmpPhaseFrequencySeries* h22 = ListmodesCAmpPhaseFrequencySeries_GetMode(listROM, 2, 2)->freqseries;
    double fLowROM22 = gsl_vector_get(h22->freq, 0);
    double fHighROM22 = gsl_vector_get(h22->freq, h22->freq->size - 1);
    double f1windowbeg = fmax(fLow, fLowROM22);
    double f2windowbeg = 1.1 * f1windowbeg; /* Here hardcoded relative width of the window */
    double f2windowend = fHighROM22;
    double f1windowend = 0.995 * f2windowend; /* Here hardcoded relative width of the window */

    /* Compute h22 TD by IFFT */
    ReImTimeSeries* h22TD = NULL;
    IFFTFrequencySeries(&h22TD, h22tilde, f1windowbeg, f2windowbeg, f1windowend, f2windowend, 1); /* Here hardcoded nzeropadding */

    //
    printf("length h22TD %d\n", h22TD->times->size);
    //exit(0);

    /* Output */
    Write_Wave_h22TD(params->outdir, params->outfile, h22TD, params->binaryout);
    exit(0);
  }
}
