#include <stdio.h>
#include <stdlib.h>
#include "svmTrainMain.hpp"
#include "svmTrain.h"
#include "parse.hpp"
#include <iostream>
#include <vector>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <vector>
#include "CycleTimer.h"
#include <fstream>
#include <sstream>

state_model state;

using namespace std;

static void usage_exit() {
    cerr <<
"   Command Line:\n"
"\n"
"   -a/--num-att        :  [REQUIRED] The number of attributes\n"
"									  /features\n"
"   -x/--num-ex       	:  [REQUIRED] The number of training \n"
"									  examples\n"
"   -f/--file-path      :  [REQUIRED] Path to the training file\n"
"   -c/--cost        	:  Parameter c of the SVM (default 1)\n"
"   -g/--gamma       	:  Parameter gamma of the radial basis\n"
"						   function: exp(-gamma*|u-v|^2)\n"
"						   (default: 1/num-att)"
"   -e/--epsilon        :  Tolerance of termination criterion\n"
"						   (default 0.001)"
"	-n/--max-iter		:  Maximum number of iterations\n"
"						   (default 150,000"
"	-m/--model 			:  [REQUIRED] Path of model to be saved\n"
"	-s/--cache-size		:  Size of cache (num cache lines)\n"
"\n";
    
	exit(-1);
}

static struct option longOptionsG[] =
{
    { "num-att",        required_argument,          0,  'a' },
    { "num-ex",         required_argument,          0,  'x' },
    { "cost",           required_argument,          0,  'c' },
    { "gamma",          required_argument,          0,  'g' },
    { "file-path",      required_argument,          0,  'f' },
    { "epsilon",       	required_argument,          0,  'e' },
    { "max-iter",		required_argument,			0,	'n'	},
    { "model",			required_argument,			0,	'm' },
    { "cache-size",		required_argument,			0,	's' },
    { 0,                0,                          0,   0  }
};

static void parse_arguments(int argc, char* argv[]) {

    // Default Values
    state.epsilon = 0.001;
    state.c = 1;
	state.num_attributes = -1;
	state.num_train_data = -1;
	state.gamma = -1;
	strcpy(state.input_file_name, "");
	strcpy(state.model_file_name, "");
	state.max_iter = 150000;
	state.cache_size = 10;

    // Parse args
    while (1) {
        int idx = 0;
        int c = getopt_long(argc, argv, "a:x:c:g:f:e:n:m:s:", longOptionsG, &idx);

        if (c == -1) {
            // End of options
            break;
        }

        switch (c) {
        case 'a':
            state.num_attributes = atoi(optarg);
            break;
        case 'x':
            state.num_train_data = atoi(optarg);
            break;
        case 'c':
            state.c = atof(optarg);
            break;
        case 'g':
            state.gamma = atof(optarg);
            break;
       	case 'f':
            strcpy(state.input_file_name, optarg);
            break;
       	case 'e':
            state.epsilon = atof(optarg);
            break;
       	case 'n':
       		state.max_iter = atoi(optarg);
       		break;
       	case 'm':
       		strcpy(state.model_file_name, optarg);
       		break;
       	case 's':
       		state.cache_size = atoi(optarg);
       		break;
        default:
            cerr << "\nERROR: Unknown option: -" << c << "\n";
            // Usage exit
            usage_exit();
        }
    }

	if(strcmp(state.input_file_name,"")==0 || strcmp(state.model_file_name,"")==0) {

		cerr << "Enter a valid file name\n";
		usage_exit();
	}

	if(state.num_attributes <= 0 || state.num_train_data <= 0) {

		cerr << "Missing a required parameter, or invalid parameter\n";
		usage_exit();

	}

	if(state.gamma < 0) {

		state.gamma = 1 / state.num_attributes;
	}

}


int main(int argc, char *argv[]) {

	//Obtain the command line arguments
	parse_arguments(argc, argv);

	//input data attributes and labels
	std::vector<float> x(state.num_train_data * state.num_attributes,0);
	std::vector<int> y(state.num_train_data,0);

	//read data from input file
	cout << state.num_train_data << ", " << state.num_attributes << ": " << state.input_file_name << "\n";

	populate_data(x, y, state.num_train_data, state.num_attributes, state.input_file_name);
	cout << "Populated Data from input file\n";

	SvmTrain svm;

	svm.setup(x, y);
	
	unsigned long long start;
	start = CycleTimer::currentSeconds();

	int num_iter = 0;

	do {

		svm.train_step();		

		num_iter++;

//		cout << num_iter << "\n";

	} while((svm.b_lo > (svm.b_hi +(2*state.epsilon))) && num_iter < state.max_iter);
	
	unsigned long long t2 = CycleTimer::currentSeconds();
	cout << "TOTAL TIME TAKEN in seconds: " << t2-start << "\n";

	//check if converged or max_iter stop
	if(svm.b_lo > (svm.b_hi + (2*state.epsilon))) {
		cout << "Could not converge in " << num_iter << " iterations. SVM training has been stopped\n";
	} else {
		cout << "Converged at iteration number: " << num_iter << "\n";
	}

	svm.destroy_cuda_handles();

	//obtain final b intercept
	svm.b = (svm.b_lo + svm.b_hi)/2;
	cout << "b: " << svm.b << "\n";

	svm.test_setup();

	//obtain training accuracy
	float train_accuracy = svm.get_train_accuracy();
	cout << "Training accuracy: " << train_accuracy << "\n";

	svm.destroy_t_cuda_handles();

	cout << "Writing out model\n";

	float* raw_x = &x[0];
	int* raw_y = &y[0];
	thrust::host_vector<float> alpha_copy = svm.get_g_alpha();
	float* raw_alpha = thrust::raw_pointer_cast(&alpha_copy[0]);	

	//write model to file
	write_out_model(raw_x, raw_y, raw_alpha, svm.b);

	cout << "Training model has been saved to the file " << state.model_file_name << "\n";

	return 0;
}

void write_out_model(float* x, int* y, float* alpha, float b) {
	//open output filestream for writing the model
	ofstream model_file;
	model_file.open(state.model_file_name);

	if(model_file.is_open()) {
		//gamma used in kernel for training
		model_file << state.gamma << "\n";
		model_file << b << "\n";

		for(int i=0; i<state.num_train_data; i++) {
			if(alpha[i] != 0) {
				model_file << alpha[i] << "," << y[i];

				//index for x
				int idx = i*(state.num_attributes);

				for(int j=0; j<state.num_attributes; j++) {
					model_file << "," << x[idx + j];
				}

				model_file << "\n";
			}
		}

		model_file.close();
	} else {
		cout << "Model output file " << state.model_file_name << " could not be opened for writing.\n";
		exit(-1);
	}
}
