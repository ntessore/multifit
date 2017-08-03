#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "mpfit.h"
#include "mpis.h"
#include "input.h"

const char* USAGE =
"usage: ptmatch [-vqux] [-I MAXITER] [-o OUTFILE] [-m MATFILE]\n"
"               [-a ANCFILE] [-n NSAMPLE] [-s SAMFILE] PTSFILE";

// weighted distance between observed points and mapped reference points
int mapdist(int m, int n, double* p, double* d, double** dd, void* private)
{
    // problem description
    const int ni = n/5;
    const int nx = m/(ni - 1)/2;
    const double* x = private;
    
    // anchor point of image 0
    const double* a0 = p;
    
    // reference shear
    const double g1 = p[3];
    const double g2 = p[4];
    
    // number of deviates computed
    int k = 0;
    
    // try to map points from image 0 to image i
    for(int i = 1; i < ni; ++i)
    {
        // anchor point of image
        const double x0[2] = { p[5*i+0], p[5*i+1] };
        
        // matrix coefficients
        const double A = p[5*i+2];
        const double B = p[5*i+3];
        const double C = g2*A - g1*B;
        const double D = p[5*i+4];
        
        // transformation matrix
        const double T[4] = { 0.5*(D + A), 0.5*(B - C),
                              0.5*(B + C), 0.5*(D - A) };
        
        // map points, apply whitening transform and compute distance
        for(int j = 0; j < nx; ++j)
        {
            // reference point
            const double aj[2] = { x[5*nx*0+5*j+0], x[5*nx*0+5*j+1] };
            
            // observed point
            const double xj[2] = { x[5*nx*i+5*j+0], x[5*nx*i+5*j+1] };
            
            // whitening transform for observed point
            const double W[4] = { x[5*nx*i+5*j+2], x[5*nx*i+5*j+3],
                                                0, x[5*nx*i+5*j+4] };
            
            // offset between predicted and observed position
            const double delta[2] = {
                (xj[0] - x0[0]) - T[0]*(aj[0] - a0[0]) - T[1]*(aj[1] - a0[1]),
                (xj[1] - x0[1]) - T[2]*(aj[0] - a0[0]) - T[3]*(aj[1] - a0[1])
            };
            
            // compute uncorrelated deviates
            d[k+0] = W[0]*delta[0] + W[1]*delta[1];
            d[k+1] = W[2]*delta[0] + W[3]*delta[1];
            
            // compute derivatives if asked to
            if(dd)
            {
                // derivatives for reference shear
                if(dd[3])
                {
                    dd[3][k+0] = -0.5*B*(+ W[0]*(aj[1] - a0[1])
                                         - W[1]*(aj[0] - a0[0]));
                    dd[3][k+1] = -0.5*B*(+ W[2]*(aj[1] - a0[1])
                                         - W[3]*(aj[0] - a0[0]));
                }
                if(dd[4])
                {
                    dd[4][k+0] = +0.5*A*(+ W[0]*(aj[1] - a0[1])
                                         - W[1]*(aj[0] - a0[0]));
                    dd[4][k+1] = +0.5*A*(+ W[2]*(aj[1] - a0[1])
                                         - W[3]*(aj[0] - a0[0]));
                }
                
                // derivatives for anchor point
                if(dd[5*i+0])
                {
                    dd[5*i+0][k+0] = -W[0];
                    dd[5*i+0][k+1] = -W[2];
                }
                if(dd[5*i+1])
                {
                    dd[5*i+1][k+0] = -W[1];
                    dd[5*i+1][k+1] = -W[3];
                }
                
                // derivatives for a, b, d coefficients
                if(dd[5*i+2])
                {
                    dd[5*i+2][k+0] = -0.5*(+ W[0]*(aj[0] - a0[0])
                                           - W[0]*g2*(aj[1] - a0[1])
                                           - W[1]*(aj[1] - a0[1])
                                           + W[1]*g2*(aj[0] - a0[0]));
                    dd[5*i+2][k+1] = -0.5*(+ W[2]*(aj[0] - a0[0])
                                           - W[2]*g2*(aj[1] - a0[1])
                                           - W[3]*(aj[1] - a0[1])
                                           + W[3]*g2*(aj[0] - a0[0]));
                }
                if(dd[5*i+3])
                {
                    dd[5*i+3][k+0] = -0.5*(+ W[0]*(1 + g1)*(aj[1] - a0[1])
                                           + W[1]*(1 - g1)*(aj[0] - a0[0]));
                    dd[5*i+3][k+1] = -0.5*(+ W[2]*(1 + g1)*(aj[1] - a0[1])
                                           + W[3]*(1 - g1)*(aj[0] - a0[0]));
                }
                if(dd[5*i+4])
                {
                    dd[5*i+4][k+0] = -0.5*(+ W[0]*(aj[0] - a0[0])
                                           + W[1]*(aj[1] - a0[1]));
                    dd[5*i+4][k+1] = -0.5*(+ W[2]*(aj[0] - a0[0])
                                           + W[3]*(aj[1] - a0[1]));
                }
            }
            
            // done with deviates
            k += 2;
        }
    }
    
    // success if all deviates have been computed
    return k == m ? 0 : -1;
}

// convert g,a,b,d parameters to convergence ratio f and reduced shear g
void ptofg(int ni, double p[])
{
    // reference shear
    const double g1 = p[3];
    const double g2 = p[4];
    
    for(int i = 1; i < ni; ++i)
    {
        // a,b,c,d coefficients for image
        const double A = p[5*i+2];
        const double B = p[5*i+3];
        const double C = g2*A - g1*B;
        const double D = p[5*i+4];
        
        // numerator and denominator for f and g
        const double J = 0.5*(C*C + D*D - A*A - B*B);
        const double P = D*g1 - C*g2 + A;
        const double Q = C*g1 + D*g2 + B;
        const double R = A*g1 + B*g2 + D;
        
        // compute f, g1, g2 for image
        p[5*i+2] = J/R;
        p[5*i+3] = P/R;
        p[5*i+4] = Q/R;
    }
}

int main(int argc, char* argv[])
{
    // error indicator and message
    int err = 0;
    const char* msg = NULL;
    
    // input
    char* ptsfile;
    char* outfile;
    char* matfile;
    char* ancfile;
    char* samfile;
    int v, xmode, no_uncert, maxiter, nsample, ND, DD, seed;
    
    // number of images and points
    int ni, nx;
    double* x;
    
    // parameters
    int np;
    double* p;
    double* cov;
    
    // optimiser data
    int       nd;
    mp_par*   par;
    mp_config cfg = {0};
    mp_result res = {0};
    
    
    /*********
     * input *
     *********/
    
    // default arguments
    ptsfile = outfile = matfile = ancfile = samfile = NULL;
    v = xmode = no_uncert = maxiter = nsample = ND = DD = seed = 0;
    
    // parse arguments
    for(int i = 1; i < argc && !err; ++i)
    {
        if(argv[i][0] == '-')
        {
            // flags
            for(char* c = &argv[i][1]; *c; ++c)
            {
                // increase verbosity
                if(*c == 'v')
                {
                    v += 1;
                }
                // decrease verbosity
                else if(*c == 'q')
                {
                    v -= 1;
                }
                // ignore uncertainties
                else if(*c == 'u')
                {
                    if(!no_uncert)
                        no_uncert = 1;
                    else
                        err = 1;
                }
                // number of iterations
                else if(*c == 'I')
                {
                    if(!maxiter && i + 1 < argc)
                        maxiter = atoi(argv[++i]);
                    else
                        err = 1;
                }
                // output file
                else if(*c == 'o')
                {
                    if(!outfile && i + 1 < argc)
                        outfile = argv[++i];
                    else
                        err = 1;
                }
                // output matrix file
                else if(*c == 'm')
                {
                    if(!matfile && i + 1 < argc)
                        matfile = argv[++i];
                    else
                        err = 1;
                }
                // output anchor file
                else if(*c == 'a')
                {
                    if(!ancfile && i + 1 < argc)
                        ancfile = argv[++i];
                    else
                        err = 1;
                }
                // expectation mode
                else if(*c == 'x')
                {
                    if(!xmode)
                        xmode = 1;
                    else
                        err = 1;
                }
                // number of samples
                else if(*c == 'n')
                {
                    if(!nsample && i + 1 < argc)
                        nsample = atoi(argv[++i]);
                    else
                        err = 1;
                }
                // output samples file
                else if(*c == 's')
                {
                    if(!samfile && i + 1 < argc)
                        samfile = argv[++i];
                    else
                        err = 1;
                }
                // numerical derivatives (undocumented)
                else if(*c == 'N')
                {
                    ND = 1;
                }
                // debug derivatives (undocumented)
                else if(*c == 'D')
                {
                    DD = 1;
                }
                // seed (undocumented)
                else if(*c == 'S')
                {
                    if(!seed && i + 1 < argc)
                        seed = atoi(argv[++i]);
                    else
                        err = 1;
                }
                // unknown flag
                else
                {
                    err = 1;
                }
            }
        }
        else
        {
            // positional arguments
            if(!ptsfile)
                ptsfile = argv[i];
            else
                err = 1;
        }
    }
    
    // make sure input file was given
    if(!ptsfile)
        err = 1;
    
    // sample output requires expectation mode
    if(samfile && !xmode)
    {
        msg = "sample output (-s) requires expectation mode (-x)";
        err = 1;
    }
    
    // check for input errors
    if(err)
        goto err_usage;
    
    // read points from input file
    read_points(ptsfile, &ni, &nx, &x);
    
    // make sure that enough images were given
    if(ni < 3)
    {
        fprintf(stderr, "%s: needs at least three images\n", ptsfile);
        return EXIT_FAILURE;
    }
    
    // make sure that enough points were given
    if(nx < 3)
    {
        fprintf(stderr, "%s: needs at least three points\n", ptsfile);
        return EXIT_FAILURE;
    }
    
    // compute points' whitening transform from input covariances
    for(int i = 0; i < ni; ++i)
    {
        for(int j = 0; j < nx; ++j)
        {
            // covariance matrix
            const double s1  = x[nx*5*i+5*j+2];
            const double s2  = x[nx*5*i+5*j+3];
            const double rho = x[nx*5*i+5*j+4];
            
            // check that given covariance is sane
            if(s1 < 0 || s2 < 0 || rho*rho >= 1)
            {
                fprintf(stderr, "%s: invalid covariance matrix for point %d "
                        "of image %d\n", ptsfile, j, i);
                return EXIT_FAILURE;
            }
            
            // check if uncertainties should be ignored
            if(no_uncert)
            {
                // default values
                x[nx*5*i+5*j+2] = 1;
                x[nx*5*i+5*j+3] = 0;
                x[nx*5*i+5*j+4] = 1;
            }
            else
            {
                // Cholesky decomposition of inverse covariance matrix
                x[nx*5*i+5*j+2] = 1/(sqrt(1-rho*rho)*s1);
                x[nx*5*i+5*j+3] = -rho/(sqrt(1-rho*rho)*s2);
                x[nx*5*i+5*j+4] = 1/s2;
            }
        }
    }
    
    
    /**************
     * initialise *
     **************/
    
    // total number of parameters, including fixed ones
    np = 5*ni;
    
    // create arrays for parameters and covariance matrix
    p = malloc(np*sizeof(double));
    cov = malloc(np*np*sizeof(double));
    if(!p || !cov)
        goto err_malloc;
    
    // set initial anchor points to centroids for all images
    for(int i = 0; i < ni; ++i)
    {
        p[5*i+0] = 0;
        p[5*i+1] = 0;
        for(int j = 0; j < nx; ++j)
        {
            p[5*i+0] += (x[nx*5*i+5*j+0] - p[5*i+0])/(j + 1);
            p[5*i+1] += (x[nx*5*i+5*j+1] - p[5*i+1])/(j + 1);
        }
    }
    
    // initial convergence ratio and shear for image 0
    p[2] = 1;
    p[3] = 0;
    p[4] = 0;
    
    // set a,b,d coefficients for multiple image to identity matrix
    for(int i = 1; i < ni; ++i)
    {
        p[5*i+2] = 0;
        p[5*i+3] = 0;
        p[5*i+4] = 2;
    }
    
    // output initial parameter guess if very verbose
    if(v > 1)
    {
        printf("initial parameters:\n");
        for(int i = 0; i < ni; ++i)
        {
            for(int j = 0; j < 5; ++j)
                printf("  % 10.4f", p[5*i+j]);
            printf("\n");
        }
    }
    
    
    /************
     * minimise *
     ************/
    
    // number of data points
    nd = 2*nx*(ni - 1);
    
    // parameter configuration
    par = calloc(np, sizeof(mp_par));
    if(!par)
        goto err_malloc;
    
    // fix reference image anchor point and convergence ratio
    par[0].fixed = 1;
    par[1].fixed = 1;
    par[2].fixed = 1;
    
    // set up parameters
    for(int i = 0; i < np; ++i)
    {
        // step size
        par[i].step = 1e-10;
        
        // numerical or analytical derivatives
        par[i].side = ND ? 0 : 3;
        
        // debug derivatives
        par[i].deriv_debug = DD;
    }
    
    // MPFIT configuration
    cfg.ftol    = 1e-16;
    cfg.xtol    = 1e-12;
    cfg.gtol    = 1e-12;
    cfg.maxiter = maxiter;
    cfg.nprint  = (v > 1 ? 1 : 0);
    
    // set up results structure
    res.covar = cov;
    
    // minimise weighted distance between observed and mapped points
    err = mpfit(mapdist, nd, np, p, par, &cfg, x, &res);
    
    // check for errors
    if(err <= 0)
        goto err_mpfit;
    
    // output convergence reason if verbose
    if(v > 0)
    {
        const char* msg = NULL;
        switch(err)
        {
            case MP_OK_CHI:
                msg = "Convergence in chi-square value";
                break;
            case MP_OK_PAR:
                msg = "Convergence in parameter value";
                break;
            case MP_OK_BOTH:
                msg = "Both MP_OK_PAR and MP_OK_CHI hold";
                break;
            case MP_OK_DIR:
                msg = "Convergence in orthogonality";
                break;
            case MP_MAXITER:
                msg = "Maximum number of iterations reached";
                break;
            case MP_FTOL:
                msg = "ftol is too small; no further improvement";
                break;
            case MP_XTOL:
                msg = "xtol is too small; no further improvement";
                break;
            case MP_GTOL:
                msg = "gtol is too small; no further improvement";
                break;
        }
        if(msg)
            printf("MPFIT success: %s \n", msg);
        else
            printf("MPFIT success: %d \n", err);
    }
    
    // output results structure if very verbose
    if(v > 1)
    {
        printf("MPFIT results:\n");
        printf("  bestnorm  = %16f\n", res.bestnorm);
        printf("  orignorm  = %16f\n", res.orignorm);
        printf("  niter     = %16d\n", res.niter);
        printf("  nfev      = %16d\n", res.nfev);
        printf("  status    = %16d\n", res.status);
        printf("  npar      = %16d\n", res.npar);
        printf("  nfree     = %16d\n", res.nfree);
        printf("  npegged   = %16d\n", res.npegged);
        printf("  nfunc     = %16d\n", res.nfunc);
    }
    
    
    /********************
     * expectation mode *
     ********************/
    
    // compute expectation values when `-x` is given
    if(xmode)
    {
        // samples
        int ns;
        double* s;
        
        // number of samples, use default if not specified
        ns = nsample ? nsample : 10000;
        
        // allocate space for samples
        s = malloc(ns*(np+2)*sizeof(double));
        if(!s)
            goto err_malloc;
        
        // seed random number generator
        srand(seed ? seed : time(0));
        
        // status output if verbose
        if(v > 0)
            printf("expectation mode: %d samples\n", ns);
        
        // draw samples
        err = mpis(mapdist, nd, np, p, cov, x, ns, s);
        if(err <= 0)
            goto err_mpfit;
        
        // output effective number of samples if verbose
        if(v > 0)
            printf("effective number of samples: %d\n",
                   (int)mpis_neff(np, ns, s));
        
        // convert sampled parameters to f and g
        for(int i = 0; i < ns; ++i)
            ptofg(ni, &s[i*(np+2)+2]);
        
        // compute mean value of lens quantities from samples
        mpis_stat(np, ns, s, p, NULL);
        
        // write samples if asked to
        if(samfile)
        {
            FILE* fp = fopen(samfile, "w");
            if(!fp)
            {
                msg = samfile;
                goto err_file;
            }
            for(int i = 0; i < ns; ++i)
            {
                for(int j = 0; j < np+2; ++j)
                    fprintf(fp, "  %28.18E", s[i*(np+2)+j]);
                fprintf(fp, "\n");
            }
            fclose(fp);
        }
        
        // done with samples
        free(s);
    }
    else
    {
        // convert parameters to f and g
        ptofg(ni, p);
    }
    
    
    /**********
     * output *
     **********/
    
    // print table of convergence ratios and shear if not quiet
    if(v > -1)
    {
        for(int i = 0; i < ni; ++i)
            printf("% 18.8f % 18.8f % 18.8f\n", p[5*i+2], p[5*i+3], p[5*i+4]);
    }
    
    // write convergence ratios and shears if asked to
    if(outfile)
    {
        // open matrix file for writing
        FILE* fp = fopen(outfile, "w");
        if(!fp)
        {
            msg = outfile;
            goto err_file;
        }
        
        // write convergence ratios and shears
        for(int i = 0; i < ni; ++i)
            fprintf(fp, "% 18.8f % 18.8f % 18.8f\n",
                    p[5*i+2], p[5*i+3], p[5*i+4]);
        
        // done
        fclose(fp);
    }
    
    // write transformation matrices if asked to
    if(matfile)
    {
        // open matrix file for writing
        FILE* fp = fopen(matfile, "w");
        if(!fp)
        {
            msg = matfile;
            goto err_file;
        }
        
        // write transformation matrices
        for(int i = 1; i < ni; ++i)
        {
            // convergence ratios and shears of images 0 and i
            const double g1 = p[3];
            const double g2 = p[4];
            const double F  = p[5*i+2];
            const double G1 = p[5*i+3];
            const double G2 = p[5*i+4];
            
            // matrix coefficients
            const double
            A = F*((1 - g1)*(1 + G1) - g2*G2)/(1 - G1*G1 - G2*G2),
            B = F*((1 + g1)*G2 - (1 + G1)*g2)/(1 - G1*G1 - G2*G2),
            C = F*((1 - g1)*G2 - (1 - G1)*g2)/(1 - G1*G1 - G2*G2),
            D = F*((1 + g1)*(1 - G1) - g2*G2)/(1 - G1*G1 - G2*G2);
            
            // write matrix coefficients
            fprintf(fp, "% 18.8f % 18.8f % 18.8f % 18.8f\n", A, B, C, D);
        }
        
        // done writing transforms
        fclose(fp);
    }
    
    // write anchor points if asked to
    if(ancfile)
    {
        // open anchor file for writing
        FILE* fp = fopen(ancfile, "w");
        if(!fp)
        {
            msg = ancfile;
            goto err_file;
        }
        
        // write anchor points
        for(int i = 0; i < ni; ++i)
            fprintf(fp, "% 18.8f % 18.8f\n", p[5*i+0], p[5*i+1]);
        
        // done with anchor points file
        fclose(fp);
    }
    
    
    /************
     * cleaning *
     ************/
    
    free(x);
    free(p);
    free(cov);
    free(par);
    
    
    /********
     * done *
     ********/
    
    return EXIT_SUCCESS;
    
    
    /**********
     * errors *
     **********/
    
    // print usage message, might not be an error
err_usage:
    fprintf(err ? stderr : stdout, "%s\n", USAGE);
    if(err && msg)
        fprintf(stderr, "\nerror: %s\n", msg);
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
    
    // memory allocation error
err_malloc:
    perror(NULL);
    return EXIT_FAILURE;
    
    // file error
err_file:
    perror(msg);
    return EXIT_FAILURE;
    
    // handle CMPFIT error codes
err_mpfit:
    switch(err)
    {
        case MP_ERR_INPUT:
            msg = "General input parameter error";
            break;
        case MP_ERR_NAN:
            msg = "User function produced non-finite values";
            break;
        case MP_ERR_FUNC:
            msg = "No user function was supplied";
            break;
        case MP_ERR_NPOINTS:
            msg = "No user data points were supplied";
            break;
        case MP_ERR_NFREE:
            msg = "No free parameters";
            break;
        case MP_ERR_MEMORY:
            msg = "Memory allocation error";
            break;
        case MP_ERR_INITBOUNDS:
            msg = "Initial values inconsistent w constraint";
            break;
        case MP_ERR_BOUNDS:
            msg = "Initial constraints inconsistent";
            break;
        case MP_ERR_PARAM:
            msg = "General input parameter error";
            break;
        case MP_ERR_DOF:
            msg = "Not enough degrees of freedom";
            break;
    }
    
    // print error message or code
    if(msg)
        fprintf(stderr, "MPFIT error: %s\n", msg);
    else
        fprintf(stderr, "MPFIT error: %d\n", err);
    
    return EXIT_FAILURE;
}
