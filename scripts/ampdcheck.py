#!/usr/bin/python3
"""
ampdcheck

Utility to plot the results of AMPD peak finding algorithm along with the
input and intermediate steps as well. Input can either be a singular file
or the directory cntaining all ampd aux out files.

Usage #1:
    ampdcheck [path/to/aux_batch_dir]

    This aux output of ampd consist of the files:
        raw.dat
        detrend.dat
        lms.dat
        gamma.dat
        sigma.dat
        peaks.dat
        peaknum

    ampdcheck reads these files and plots all the data in orderly manner

Usage #2:
    ampdcheck [path/to/file]

    Simply plot the data as vector or matrix, whatever it finds. 

"""
import numpy as np
import matplotlib.pyplot as plt
import argparse
import csv
import os
import glob

def main():

    parser = argparse.ArgumentParser()
    parser.add_argument('path', type=str, help='input dat file')
    #parser.add_argument('--lms', dest='lms',action='store_const',\
    #                    default=0,const=1,nargs=0,help='option to plot LMS')
    args = parser.parse_args()

    # the usual output of ampd, these should exist within the batch dir
    # if the directory is given as the argument
    datfiles = ["raw.dat","detrend.dat","gamma.dat","sigma.dat",\
                "peaks.dat","param.txt","smoothed.dat"]
    exists_lms = 0 # set 1 if lms matrix file is found, otherwise plot rest

    """
    Plot raw, smoothed, gamma, sigma, peaks first in one fig,
    the plot LMS in another fig
    """
    print("Loading data from "+args.path+"...")
    if os.path.isdir(args.path):
        # check if LMS file exists
        if "lms.dat" in os.listdir(args.path):
            exists_lms = 1

        # check if all other exists
        for f in datfiles:
            if f not in os.listdir(args.path):
                print("Cannot find file '"+f+"' exiting...\n")
                quit()
        # figure setup 
        n = len(datfiles) - 2
        fig, ax= plt.subplots(n, 1, figsize=(10,7))
        # linewidth of timeseries and gamma
        lw_def = 1
        # alpha of peak lines
        a_def = 0.5
        # load and plot data
        for f in datfiles:
            full_path = args.path + "/" + f
            if f is not "param.txt":
                data = np.loadtxt(full_path, delimiter='\t')

            if f is "raw.dat":
                ax[0].plot(data,linewidth=lw_def)
                ax[0].margins(x=0)
                datanum = len(data)
            elif f is "smoothed.dat":
                ax[1].plot(data,linewidth=lw_def)
                ax[1].margins(x=0)
            elif f is "detrend.dat":
                ax[2].plot(data,linewidth=lw_def)
                ax[2].margins(x=0)
            elif f is "gamma.dat":
                ax[3].plot(data,linewidth=lw_def)
                ax[3].margins(x=0)
            elif f is "sigma.dat":
                ax[4].plot(data,linewidth=lw_def)
                ax[4].margins(x=0)
            elif f is "peaks.dat":
                if data.ndim == 0: # hack if only one peak was found
                    data = [data]
                for point in data:
                    ax[0].axvline(x=point,color='r',zorder=0,alpha=a_def)
                    #print("ylim = " + str(ax[0].get_ylim()))
                    #ax[1].axvline(x=point,color='r',zorder=0)
                    ax[2].axvline(x=point,color='r',zorder=0,alpha=a_def)
            else:
                pass

        # load params and plot rest
        for f in datfiles:
            full_path = args.path + "/" + f
            if f is "param.txt":
                # plot on top of raw
                pdict = load_param(full_path)
                # plot vertical line on gamma min = lambda
                ax[3].axvline(int(pdict["lambda"]),color="orange")
                sampling_rate = float(pdict["sampling_rate"])
                x = np.linspace(0,datanum, datanum)
                y = float(pdict["fit_a"]) * x/sampling_rate + float(pdict["fit_b"])
                ax[0].plot(x, y, ':r')
                break

        # util formatting
        text = ['raw','detrend','smoothed','gamma','sigma']
        for i in range(n):
            ax[i].text(0.5,0.87,text[i],horizontalalignment='center',
                    transform=ax[i].transAxes)
            #ax[i].set_xticklabels([])

        plt.subplots_adjust(wspace=0,hspace=0)
        """
        Plot LMS

        """
        if exists_lms == 1:
            fig2, ax2 = plt.subplots(1,2,figsize=(10,7))
            # plot lms
            full_path = args.path + "/" + "lms.dat"
            data = np.loadtxt(full_path, delimiter='\t')
            ax2[0].imshow(data,aspect='auto')
            _lambda = int(pdict["lambda"])
            ax2[1].imshow(data[:_lambda,:],aspect='auto')
            plt.tight_layout()



        plt.show()
    if os.path.isfile(args.path):
        check_single_input(args.path)

def load_param(paramfile):
    """
    Return a dictionary from param.txt, given as input which contains
    ampd_param struct memebers in a format name=val at each line.

    """
    name = ["sampling_rate", "datatype", "a", "rnd_factor", \
            "fit_a", "fit_b", "fit_r", "lambda", "sigma_thresh",\
            "peak_thresh"]
    val = [None] * len(name)
    with open(paramfile, "r") as f:
        lines = f.read().splitlines()
        for line in lines:
            sline = line.split('=')
            for j in range(len(name)):
                if name[j] == sline[0]:
                    if len(sline) == 2:
                        val[j] = sline[1]
                    elif len(sline) == 1:
                        val[j] = None

    return dict(zip(name, val))


def check_single_input(path):
    """
    File argument only as input, plot it
    """
    if os.path.isfile(path):
        data = np.loadtxt(path, delimiter='\t')
        if len(data.shape) == 1:
            plt.plot(data)
            plt.margins(x=0)
            plt.tight_layout()
            plt.show()
        elif len(data.shape) == 2:
            plt.imshow(data)
            plt.tight_layout()
            plt.show()
        else:
            print("Cannot display data with shape "+str(data.shape))
    else:
        print("INput path was not a file")
        return 0

if __name__ == "__main__":
    main()

