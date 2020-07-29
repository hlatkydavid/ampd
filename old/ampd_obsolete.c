/*
 * ampd.c
 *
 */

#include "ampd.h"
#include "filters.h"

#define TESTING 1

static struct option long_options[] = 
{
    {"infile",required_argument, NULL, 'f'},
    {"help",optional_argument, NULL, 'h'},
    {"outfile",optional_argument, NULL, 'o'},
    {"auxdir",optional_argument, NULL, 'x'},
    {"timestep",optional_argument, NULL, 't'},
    {"overlap", optional_argument, NULL, 'p'},
    {"verbose", optional_argument, NULL, 'v'},
    {"output-all", optional_argument, NULL, 'a'},
    {"output-lms", optional_argument, NULL, 'm'},
    {"length", optional_argument, NULL, 'l'},
    {"rate",optional_argument,NULL, 'r'},
    {NULL, 0, NULL, 0}
};

/**
 * Print description and general usage
 */
void printf_help(){

    printf(
    "AMPD\n"
    "=========================================================================\n"
    "Peak detection algorithm for quasiperiodic data. Main usage of this "
    "implementation is detection of peaks in rat physiological data: "
    "respiration and pulsoxymmetry waveforms.\n\n"

    "Reference paper:\n"
    "An Efficient Algorithm for Automatic Peak Detection in Noisy "
    "Periodic and Quasi-Periodic Signals\n"
    "DOI:10.3390/a5040588\n\n"

    "This program takes a file input which contains a single timeseries "
    "of quasi-periodic data. The output is a file containing the indices "
    "of the peaks as calculated.\n"

    "Input file should only contain a float value in each line.\n"
    "Main output file contains the indices of peaks, while aux output "
    "directory contains various intermediate data for error checking. "
    "The final peak count is sent to stdout as well."
    "\n"
    "For long data files the processing is done in batches to avoid too high "
    "resource usage. This improves accuracy as well, since data homogeneity is "
    "better preserved in smaller batches. These can overlap redundantly. "
    ""

    "\n\n"

    "Usage from linux command line:\n"
    " $ ampd -f [input file]\n"
    "Optional arguments:\n"
    "\t-o --outfile:\tpath to main output file\n"
    "\t-r --rate:\toutput peak-per-min\n"
    "\t-v --verbose:\tverbose\n"
    "\t-h --help:\tprint help\n"
    "\t-a --output-all:\toutput aux data, local maxima scalogram not included\n"
    "\t-m --output-lms:\toutput local maxima scalogram (high disk space usage)\n"
    "\t-x --auxdir:\taux data root dir, default is cwd\n"
    "\t-t --timestep:\ttime resolution of input data\n"
    "\t-p --overlap:\tmake batches overlapping in time domain\n"
    "\t-l --length:\tdata window length in seconds\n"
    "\n"
            );
}
/**
 * Main handles the command line input parsing and output file management.
 */
int main(int argc, char **argv){

    int opt;
    int verbose = VERBOSE;
    int output_all = OUTPUT_ALL;
    int output_lms = OUTPUT_LMS;
    int output_rate = OUTPUT_RATE;
    double overlap = OVERLAP_DEF;   //overlap ratio
    int n_ovlap;             // overlapping data points

    // timing
    clock_t begin, end;
    double time_spent;

    char infile[MAX_PATH_LEN] = {0};
    char infile_basename[MAX_PATH_LEN]; // input, without dir and extension
    char outfile[MAX_PATH_LEN] = {0}; // main output file with indices of peaks
    char cwd[MAX_PATH_LEN]; // current directory
    FILE *fp_out;
    // aux output files
    char raw_path[MAX_PATH_LEN];
    char detrend_path[MAX_PATH_LEN];
    char lms_path[MAX_PATH_LEN];
    char rlms_path[MAX_PATH_LEN];
    char gamma_path[MAX_PATH_LEN];
    char sigma_path[MAX_PATH_LEN];
    char peaks_path[MAX_PATH_LEN];
    char linfit_path[MAX_PATH_LEN];
    char smoothed_path[MAX_PATH_LEN];

    // batch processing
    float *data;        //  timeseries
    int n;              // number of elements in timeseries, in a batch, dynamic
    int ind;            // index of next batch
    int data_buf = DATA_BUF_DEF;
    int datalen;        // full data length
    int cycles;         // number of data batches
    int n_peaks;        // main output, number of peaks
    int sum_n_peaks;    // summed peak number from all batches
    int *sum_peaks;      // concatenated vector from peak indices
    double ts = TIMESTEP_DEFAULT;           // timestep
    double thresh = PEAK_MIN_DIST;          // peak distance threshold in seconds
    double data_buf_sec = 0.0;
    double a, b; // linear fitting
    // ampd routine pointers
    int l;
    struct Mtx *lms;
    double *gamma;
    double *sigma;
    int *peaks;

    // main output file, containing only the peak indices
    char outfile_def[] = "ampd.out.peaks"; //

    // aux output paths
    char aux_dir[MAX_PATH_LEN] = {0};
    char batch_dir[MAX_PATH_LEN] = {0};
    char aux_dir_def[] = "ampd_out"; // full default is cwd plus this
    // parse options
    while((opt = getopt_long(argc,argv,"hmvf:o:ax:p:l:",long_options,NULL)) != -1){
        switch(opt){
            case 'h':
                printf_help();
                return 0;
            case 'v':
                verbose = 1;
                break;
            case 'o':
                strcpy(outfile, optarg);
                break;
            case 'f':
                strcpy(infile, optarg);
                break;
            case 'a':
                output_all = 1;
                break;
            case 'm':
                output_lms = 1;
                break;
            case 't':
                ts = atoi(optarg);
                break;
            case 'x':
                strcpy(aux_dir, optarg);
                break;
            case 'p':
                overlap = atof(optarg);
                if(overlap > 1){
                    printf("error: overlap higher than 1.0 not permitted\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'l':
                data_buf_sec = atof(optarg);
                break;

        }
    }
    /* Setting up output paths.
     * Main output path is given as command line argument, if not it is the cwd
     * General format of aux output files:
     * aux_root/data_ID/batch_[i]/out_intermediate_files
     * aux_root is the current working dir by default, but can be set with a
     * command line argument. data_ID contains the name of the file where the
     * input data is coming from.
     */
    begin = clock();
    getcwd(cwd, sizeof(cwd));
    extract_raw_filename(infile, infile_basename, sizeof(infile_basename));
    // setting to defaults if arguments were not given
    if(strcmp(outfile,"")==0){
        snprintf(outfile, sizeof(outfile), "%s/%s",cwd, outfile_def);
    }
    if(strcmp(aux_dir,"")==0){
        snprintf(aux_dir, sizeof(aux_dir), "%s/%s",cwd, aux_dir_def);
    }
    if(data_buf_sec != 0.0){
        printf("here!\n");
        data_buf = (int)(data_buf_sec / ts);
    }
    // setting remaining variables for processing
    sum_n_peaks = 0;
    datalen = count_char(infile, '\n');
    n_ovlap = (int)((double)data_buf * overlap);
    cycles = (int) (ceil(datalen / (double) (data_buf - n_ovlap)));
    fp_out = fopen(outfile, "w");
    if(fp_out == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    if(verbose == 1){
        printf("infile: %s\n", infile);
        printf("outfile: %s\n", outfile);
        printf("aux_dir: %s\n", aux_dir);
        printf("datalen: %d\n", datalen);
        printf("timestep: %.5lf\n",ts);
        printf("data_buf: %d\n",data_buf);
        printf("cycles: %d\n", cycles);
        printf("output-all: %d\n",output_all);

    }
    for(int i=0; i<cycles; i++){
        if(TESTING == 1){
            if(i>0)
                break;
        }

        // settings aux output paths
        snprintf(batch_dir, sizeof(batch_dir),"%s/batch_%d",aux_dir,i);
        snprintf(detrend_path,sizeof(detrend_path),"%s/detrend.dat",batch_dir);
        snprintf(raw_path,sizeof(raw_path),"%s/raw.dat",batch_dir);
        snprintf(lms_path,sizeof(lms_path),"%s/lms.dat",batch_dir);
        snprintf(rlms_path,sizeof(lms_path),"%s/rlms.dat",batch_dir);
        snprintf(gamma_path, sizeof(gamma_path),"%s/gamma.dat",batch_dir);
        snprintf(sigma_path, sizeof(sigma_path),"%s/sigma.dat",batch_dir);
        snprintf(peaks_path, sizeof(peaks_path),"%s/peaks.dat",batch_dir);
        snprintf(linfit_path, sizeof(linfit_path),"%s/linfit.dat",batch_dir);
        snprintf(smoothed_path, sizeof(smoothed_path),"%s/smoothed.dat",batch_dir);

        // getting data
        ind = i * (int)(data_buf - n_ovlap);
        if(i == cycles-1){
            break; //TODO del test
            //n_init = datalen - i * (int)data_buf;
            n = datalen - i * (int)data_buf;
        }
        else {
            //n_init = (int)data_buf;
            n = (int)data_buf;
        }
        //int wh = calc_halfwindow(ts, (double)SMOOTH_TIMEWINDOW);
        data = malloc(sizeof(float)*n);
        fetch_data(infile, data, n, ind);
        if(output_all == 1)
            save_data(data, n, raw_path); // save raw data
        //movingavg(data, n, 5);
        if(output_all == 1)
            save_data(data, n, smoothed_path); // save smoothed data

        // init matrix and other array pointers
        l = (int)ceil(n/2)-1;
        lms = malloc_mtx(l, n);
        gamma = malloc(sizeof(double)*l);
        sigma = malloc(sizeof(double)*n);
        peaks = malloc(sizeof(int)*n);
        // main ampd routine, contains malloc
        n_peaks = ampd(data, n, lms, gamma, sigma, peaks, &a, &b);
        //catch_false_pks(peaks, &n_peaks, ts, thresh);
        sum_n_peaks += (int)(n_peaks * (1.0-overlap));
        if(verbose == 1){
            printf("batch=%d, n_peaks=%d, sum_n_peaks=%d\n",i,n_peaks,sum_n_peaks);
        }

        //concat_peaks(sum_peaks, peaks, ind);
        // save aux
        if(output_all == 1){
            //save_data(data, n, raw_path); // save raw data
            save_fitdata(a,b, n, linfit_path);
            save_data(data, n, detrend_path); // save detrended data
            save_ddata(sigma, n, sigma_path);
            save_ddata(gamma, lms->rows, gamma_path);
            save_idata(peaks, n_peaks, peaks_path); // save peak indices
            if(output_lms == 1){
                save_mtx(lms, lms_path);
            }
        }

        free(data);
        free(sigma);
        free(gamma);
        free(peaks);
        for(int j=0; j<l; j++)
            free(lms->data[j]);
        free(lms->data);
        free(lms);
    }
    end = clock();
    time_spent = (double)(end - begin) / CLOCKS_PER_SEC; 
    if(verbose == 1)
        printf("runtime = %lf\n",time_spent);
    fprintf(fp_out, "%d\n", sum_n_peaks);
    fprintf(stdout, "%d\n", sum_n_peaks);
    fclose(fp_out);
    return 0;
}

/**
 * Main routine for peak detection on a dataseries
 *
 * @param data      Input dataseries, previosly loaded from file
 * @param n         Length of dataseries
 * @param lms       Local maxima scalogram matrix
 * @param rlm       Rscaled local maxima scalogram
 * @param gamma     Vector coming from the row-wise summation of the LMS matrix.
 * @param sigma     Vector from the column-wise standard deviation of the rescaled
 *                  LMS matrix
 * @param peaks     Vector containing the indices of peaks corresponding to the 
 *                  original input dataseries
 *
 * @return          Number of peaks if successful, -1 on error.
 */
int ampd(float *data, int n, struct Mtx *lms, 
         double *gamma, double *sigma, int *peaks, double *a, double *b){

    double r = 0;
    double ts = TIMESTEP_DEFAULT;
    int l;
    int n_peaks = 0;
    int lambda;
    // LMS matrix is N x L, so make L:
    *a = 0; *b = 0;
    l = (int)ceil(n / 2) - 1;
    linear_fit(data, n, ts, a, b, &r);
    if(r != r){ // return if fitting was not working
        fprintf(stderr, "ampd: linear fit error r=%lf\n",r);
        return -1;
    }
    linear_detrend(data, n, ts, *a, *b);
    calc_lms(lms, data);
    row_sum_lms(lms, gamma);
    lambda = argmin_minind(gamma, l);
    //lambda = find_lambda(gamma, l)
    col_stddev_lms(lms, sigma, lambda);
    printf("lambda=%d\n",lambda);
    find_peaks(sigma, n, peaks, &n_peaks);
    //printf("n_peaks=%d\n",n_peaks);
    return n_peaks;

}

/**
 * Count occurrences of a character in a file
 *
 */
int count_char(char *path, char cc){
    int count = 0;
    FILE *fp;
    char c;
    fp = fopen(path, "r");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    for (c = getc(fp); c != EOF; c = getc(fp))
        if(c == cc)
            count++;
    fclose(fp);
    return count;
}

/**
 * Return an integer corresponding to the moving average window.
 * 
 */
int calc_halfwindow(double timestep, double timewindow){

    int wh;
    wh = (int)((timewindow / timestep) / 2);  
    return wh;
}
/**
 * Extract basename from a path, and omit extension as well.
 * Return 0 on success, -1 on error. Extracted string is put
 * into bname.
 */
void extract_raw_filename(char *path, char *bname, int buf){

    char *tmp;
    memset(bname, 0, buf);
    tmp = basename(path);
    if(strlen(tmp) > buf){
        fprintf(stderr, "extract_raw_filename: insufficent buffer\n");
        exit(EXIT_FAILURE);
    }
    for(int i=0; i<strlen(tmp); i++){
        if(tmp[i] != '.')
            bname[i] = tmp[i];
        else
            break;
    }
    return;
}

/**
 * Function: mkpath
 * ----------------
 *  Recursively create directories. Return -1 on error, 0 on success.
 *  Use mode 0755 for usual read-exec access or 0777 for read-write-exec.
 */
int mkpath(char *file_path, mode_t mode){

    assert(file_path && *file_path);
    for (char* p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(file_path, mode) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }
    return 0;
}

/**
 * Save list into file, one value per line.
 * Creates necessary directories.
 */
void save_data(float *data, int n, char *path){

    FILE *fp;
    if(mkpath(path, 0777) == -1){
        fprintf(stderr, "cannot make path %s\n",path);
        exit(EXIT_FAILURE);
    }
    fp = fopen(path, "w");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    for(int i=0; i<n; i++){
        fprintf(fp, "%.3f\n",data[i]);
    }
    fclose(fp);
    return;
}
/**
 * Save list of double into file, one value per line.
 * Creates necessary directories.
 * Same as save_data but the input is pointer to double array, and output has
 * more precision.
 */
void save_ddata(double *data, int n, char *path){

    FILE *fp;
    if(mkpath(path, 0777) == -1){
        fprintf(stderr, "cannot make path %s\n",path);
        exit(EXIT_FAILURE);
    }
    fp = fopen(path, "w");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    for(int i=0; i<n; i++){
        fprintf(fp, "%.5lf\n",data[i]);
    }
    fclose(fp);
    return;
}
/**
 * Save list of double into file, one value per line.
 * Creates necessary directories.
 * Same as save_data but the input is pointer to ins array.
 * 
 */
void save_idata(int *data, int n, char *path){

    FILE *fp;
    if(mkpath(path, 0777) == -1){
        fprintf(stderr, "cannot make path %s\n",path);
        exit(EXIT_FAILURE);
    }
    fp = fopen(path, "w");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    for(int i=0; i<n; i++){
        fprintf(fp, "%d\n",data[i]);
    }
    fclose(fp);
    return;
}
/**
 * Save linear fit line to file as a single column.
 */
void save_fitdata(double a, double b, double n, char *path){

    FILE *fp;
    int i;
    double val;
    if(mkpath(path, 0777) == -1){
        fprintf(stderr, "cannot make path %s\n",path);
        exit(EXIT_FAILURE);
    }
    fp = fopen(path, "w");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    for(i=0; i<n; i++){
        val = a * i + b;
        fprintf(fp, "%lf\n",val);
    }
    fclose(fp);
    return;

}
/**
 * Save matrix to a tab delimited file, withoud any headers.
 * Creates necessary directories.
 */
void save_mtx(struct Mtx *mtx, char *path){

    FILE *fp;
    int i, j;
    if(mkpath(path, 0777) == -1){
        fprintf(stderr, "cannot make path %s\n",path);
        exit(EXIT_FAILURE);
    }

    fp = fopen(path, "w");
    if(fp == NULL){
        perror("fopen, exiting...");
        exit(EXIT_FAILURE);
    }
    //printf("rows, cols: %d, %d\n", mtx->rows, mtx->cols);
    for(i=0; i<mtx->rows; i++){
        for(j=0; j<mtx->cols; j++){
            if(j == mtx->cols-1)
                fprintf(fp,"%.3f\n",mtx->data[i][j]);
            else
                fprintf(fp,"%.3f\t",mtx->data[i][j]);
        }
    }
    fclose(fp);
    return;
}
/**
 * Return the global minimum of a vector. If multiple minimums are found with 
 * a value difference smaller than the threshold, the one with the
 * smallest index is returned
 */
#define ARGMIN_THRESH 0.02 // threshold percentage
int argmin_minind(double *data, int n){

    int out = 0;
    double thresh = (double) ARGMIN_THRESH;
    double min = data[0];
    for(int i=0; i<n; i++){
        //if(data[i] < min && data[i]/min < 1-thresh){
        if(data[i] < min){
            min = data[i];
            out = i;
        }
    }
    return out;
}

/**
 * Load a part of the full timeseries data into memory from file.
 * File should only contain one float value on each line.
 */
#define FBUF 32
int fetch_data(char *path, float *data, int n, int ind ){

    FILE *fp;
    int i, count;
    char buf[FBUF];
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    fp = fopen(path, "r");
    if(fp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    count = 0; i = 0;
    while(fgets(buf, FBUF, fp)){
        if(count < ind){
            count++;
            continue;
        } else if(count > ind +n ){
            break;
        } else {
            sscanf(buf, "%f\n",data + i);
            count++;
            i++;
        }
    }
    fclose(fp);
    return 0;
}
/**
 * Smooth data with a simple moving window averaging approach.
 * Edges are extrapolated as constants. 
 *
 * Return pointer to new dataseries of same length.
 *
 * Window length is forced to be odd number of indices
 */
void smooth_data(float *data, int n, int wh, float *newdata, int n_new){

    int i, j;
    for(i=0; i<n_new; i++){
        newdata[i] = 0.0;
        for(j=0; j<2*wh+1; j++){
            newdata[i] += data[i+j];
        }
    }
    return;
}

/**
 * Least-squares linear regression. Data is assumed to be uniformly spaced.
 *
 */
//TODO fix this or delete, not sqrt...
int linear_fit(float *data, int n, double ts, double *a, double *b, double *r){

    double x; // time
    double sumx = 0.0;
    double sumx2 = 0.0;
    double sumxy = 0.0;
    double sumy = 0.0;
    double sumy2 = 0.0;
    double denom;
    for(int i=0; i<n;i++){
        x = (double)i * ts / n;
        sumx += x;
        sumx2 += sqrt(x);
        sumxy += x * data[i];
        sumy += data[i];
        sumy2 += sqrt(data[i]);
    }
    denom = (n * sumx2 - sqrt(sumx));
    if(denom == 0){
        //cannot solve
        *a = 0;*b = 0;*r = 0;
        return -1;
    }
    *a = (n * sumxy  -  sumx * sumy) / denom;
    *b = (sumy * sumx2  -  sumx * sumxy) / denom;
    if (r!=NULL) {
        *r = (sumxy - sumx * sumy / n) /    /* compute correlation coeff */
              sqrt((sumx2 - sqrt(sumx)/n) *
              (sumy2 - sqrt(sumy)/n));
    }
    return 0;
}

/**
 * Subtract linear trendline from data.
 *
 */
int linear_detrend(float *data, int n, double ts, double a, double b){

    for(int i=0; i<n; i++){
        data[i] -= a * i * ts / n + b ;
    }
    return 0;
}

/**
 * Allocate memory for local maxima scalogram matrix
 *
 */
struct Mtx* malloc_mtx(int rows, int cols){

    struct Mtx *mtx = malloc(sizeof(struct Mtx));
    mtx->rows = rows;
    mtx->cols = cols;
    mtx->data = malloc((mtx->rows*sizeof(float *)));
    for(int i=0; i<mtx->rows;i++){
        mtx->data[i] = malloc(mtx->cols * sizeof(float));
    }
    return mtx;
}
/**
 * Caclulate local maxima scalogram.
 * Moving window w_k = {2k | k=1,2...L} where L = ceil(n/2)
 * For the timeseries x_i the matrix elements:
 *  m_k_i = 0 if (x_i-1 > x_i-k-1 && x_i-1 > x_i+1-1)
 *  m_k_i = r + alpha othervise
 * where r is double rand[0,1]
 *
 */
void calc_lms(struct Mtx *lms, float *data){

    int i, k;
    int n, l;
    float rnd;
    float a = ALPHA;
    n = lms->cols;
    l = lms->rows;
    for(i = 0; i<n; i++){
        for(k=0; k<l; k++){
            rnd = (float) rand() / (float)RAND_MAX;
            rnd = rnd * (float)RAND_FACTOR;
            if(i < k){
                lms->data[k][i] = rnd + a;
                continue;
            }
            if(i>n-k+1){
                lms->data[k][i] = rnd + a;
                continue;
            }
            if(data[i-1] > data[i-k-1] && data[i-1] > data[i+k-1])
                lms->data[k][i] = 0.0;
            else
                lms->data[k][i] = rnd + a;
        }
    }
    return;
}
/**
 * Calculate the vector gamma, by summing the LMS row-wise.
 */
void row_sum_lms(struct Mtx *lms, double *gamma){

    int k, i;
    int l = lms->rows;
    int n = lms->cols;
    for(k=0; k<l; k++){
        gamma[k] = 0.0;
        for(i=0; i<n; i++){
            gamma[k] += lms->data[k][i]; 
        }
    }
}
/**
 * Calculate sigma, which is the column-wise standard deviation of the
 * reduced LMS matrix.
 */
void col_stddev_lms(struct Mtx *lms, double *sigma, int lambda){

    int n = lms->cols;
    double l = (double)lambda;
    int i, k;
    double sum_m_i;
    double sum_outer;
    for(i=0; i<n; i++){
        sigma[i] = 0.0;
        sum_m_i = 0.0; // divided by lambda
        for(k=0; k<lambda; k++){
            sum_m_i += lms->data[k][i] / l;
        }
        
        for(k=0; k<lambda; k++){
            //sigma[i] += sqrt(pow((lms->data[k][i]-sum_m_i), 2)) / (l-1.0);
            sigma[i] += fabs((lms->data[k][i]-sum_m_i)) / (l-1.0);
        }
    }
}
/**
 * Find the indices of peaks, which is where sigma is zero.
 * Give it to the pointer n_peaks.
 */
#define HARDTRESHOLD_PEAKS 1
void find_peaks(double *sigma, int n, int *peaks, int *n_peaks){

    int i;
    int j = 0;
    double tol = TOLERANCE;
    int ind_thresh = IND_THRESH; // omit peak if previous is closer than this
    *n_peaks = 0;
    for(i=0; i<n; i++){
        if(sigma[i] < tol){
            // check if previous peak is not too close, omit this one if it is
            if(HARDTRESHOLD_PEAKS == 1){
                if(i - peaks[j-1] > ind_thresh){
                    peaks[j] = i;
                    j++;
                    (*n_peaks)++;
                } else {
                    continue;
                }
            }
        }
    }
}
/**
 * Remove some false peaks based on hard thresholding minimum distance
 * TODO
 */
void catch_false_pks(int *pks, int *n_pks, double ts, double thresh){

    int i;
    int *pks2remove;
    int ind_thresh = (int) ((thresh / ts)/2);
}

int concat_peaks(int *sum_peaks, int sum_n, int *peaks, int n, int ind){

    return 0;
}

void printf_data(float *data, int n){

    for(int i=0; i<n; i++)
        printf("%f\n",data[i]);
    return;
}

void fprintf_data(FILE *fp, float *data, int n){

    for(int i=0; i<n; i++){
        fprintf(fp, "%.5f\n",data[i]);
    }
    return;
}
