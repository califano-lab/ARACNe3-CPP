# ARACNe3
ARACNe3 (Algorithm for the Reconstruction of Accurate Cellular Networks ver. 3) is an implementation of the ARACNe algorithm. Given a list of regulators and a gene expression matrix, ARACNe3 infers all significant regulatory interactions and outputs a Transcriptional Regulatory Network. A robust analysis will generate these networks on subsamples (called "subnetworks") and then consolidate them into a robust **consensus network**.

--- 

## Installing and Running ARACNe3

---

### Installing required libraries
ARACNe3 supports multithreading using OpenMP.  Here is **one** example of how you might download OpenMP libraries on the latest version of MacOS, with [homebrew](https://brew.sh) already installed.

```
brew install libomp  # Install OpenMP
```

### Build the CMake project
`cmake` is used to simplify cross-platform compatibility. After installing `cmake`, follow the instructions below to build ARACNe3:

```
# Clone the repo
git clone https://github.com/califano-lab/ARACNe3

# Install dependencies
cd ARACNe3/
git submodule update --init --recursive

# Build executable
mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

ARACNe3 is now located in `build/src/ARACNe3_app_release`.

---

##  Using ARACNe3

---

### ARACNe3 input files (see [file format](#input-file-format) below)
1.	A `tsv` that contains the normalized expression profile, with genes as rows and samples as columns. Make sure your file includes both the row names (gene names) and header names.
2.	List of regulators (e.g., transcription factors).

### Run ARACNe3
The ARACNe3 executable is now in `build/src/ARACNe3_app_release`. You can now run it on your data:
```
./build/src/ARACNe3_app_release -e /path/to/matrix.txt -r /path/to/regulators.txt -o /path/to/desired/output/directory
```

### ARACNe3 output files
ARACNe3 outputs a **consensus network** in the directory provided by the user (e.g., `-o outputdir/`).  If ARACNe3 is run with `--runid abc`, within `outputdir/` the **consensus network** is the file `outputdir/network_abc.tsv`, which summarizes each edge of the network in five columns:
1.	The regulator.
2.	The target.
3.	The APMI of the pair.
4.	The Spearman correlation of the pair.
5.	The _p_-value of the edge, based on how many subnetworks in which it appeared (see the [manuscript](https://github.com/califano-lab/ARACNe3) for methodology).

### (Optional) Using an HPC cluster
When dealing with many samples, individual subnetworks may take a very long time to compute, making an ensemble of, e.g. 100 subnetworks (`-x 100`) take several days. If you find yourself wanting to use a cluster computer to divide this labor, this is possible.

In short, you should send independent jobs of fewer subnetworks (e.g. `-x 10`) to cluster nodes, target the same output directory, but give each job its own `--runid` option. Send the parameter `--skip-consolidate` to avoid making a **consensus network** on 10 subnetworks, one for each job, and instead save the subnetworks (`--skip-consolidate` automatically also passes `--save-subnetworks`). Once all jobs are complete, run ARACNe3 again with `--consolidate-mode`, pointing `-o` to the original output directory of all jobs. ARACNe3 will enter consolidate mode, the subnetworks and their log files in, and then output a single **consensus network** of all 100 subnetworks. [Go to example 5](#example-5) to see a worked example.

---

##  Parameters

---

### Required
`-e <file>` is the expression file.

`-r <file>` is the list of regulators (e.g., TFs).

`-o <directory>` is the output directory.

### Useful
`-x <#>` is the fixed number of subnetworks to generate (default: `-x 1`).

`--adaptive` changes the meaning of `-x` to specify regulon occupancy. Instead of generating a fixed number of subnetworks, ARACNe3 will continuously generate subnetworks until finding at least `-x <#>` targets *per regulator* across all subnetworks (default: `-x 30` if `--adaptive` is specified).

`--threads <#>` sets the number of threads to use (default: `--threads 1`).

`--runid <id>` passes an identifier to replace `defaultid` in `network_defaultid.txt`.

`--skip-consolidate` skips consolidating subnetworks. Automatically also passes `--save-subnetworks`. You _should not_ change any file names of subnetworks and subnetwork logs generated by ARACNe3 to use them with `--consolidate-mode`.

`--consolidate-mode` enters consolidate mode. `-e <file>` and `-r <file>` must still be provided, with `-o <directory>` specifying a location containing both `subnetworks/` and `log-subnetworks/`. `-x <#>` specifies how many subnetwork files to use.

### Optional
`--subsample <#>` sets the subsample percent ($1-e^{-1}$ is default: `--subsample 0.63212`). Useful if you have thousands of samples.

`--alpha <#>` sets the cutoff for False Discovery Rate (FDR) pruning (default: `--alpha 0.05`). The FDR cutoff is the first pruning step, which rejects the null hypothesis for edges based on the Benjamini-Hochberg Procedure.

`--seed <#>` sets the seed (e.g.: `--seed 9001`).

`--save-subnetworks` saves subnetworks and subnetwork logs in the output directory.

`--suppress-log` stops the logging the ARACNe3 runtime.

`--FWER` prunes by control of Family Wise Error Rate (FWER) (using `--alpha <#>`), instead of FDR.

`-v` prints all outputs.

---

##  Examples

---

Note: the examples have been written based on the provided test sets: `test/exp_mat.txt` (the normalized expression matrix) and `test/regulators.txt` (the list of regulators).

### Example 1: generate one ARACNe3 subnetwork, subsampling $1-e^{-1}%$ of expression profiles, controlling for FDR < 0.05, using only one CPU core
```
/location/of/ARACNe3/build/src/ARACNe3_app_release -e test/exp_mat.txt -r test/regulators.txt -o test/output
```

### Example 2: generate one ARACNe3 subnetwork with no pruning steps, no subsampling, and seed equal to 343, using 10 CPU cores
```
/location/of/ARACNe3/build/src/ARACNe3_app_release -e test/exp_mat.txt -r test/regulators.txt -o test/output --subsample 1.00 --noAlpha --noMaxEnt --seed 343 --threads 10
```

### Example 3: generate one ARACNe3 subnetwork with all pruning steps, subsampling 33.3% of profiles, controlling for FDR < 0.01
```
/location/of/ARACNe3/build/src/ARACNe3_app_release -e test/exp_mat.txt -r test/regulators.txt -o test/output --subsample 0.333 --alpha 0.01
``` 

### Example 4: generate thirty ARACNe3 subnetworks, subsampling $1-e^{-1}%$ of expression profiles, controlling for FDR < 0.05
```
/location/of/ARACNe3/build/src/ARACNe3_app_release -e test/exp_mat.txt -r test/regulators.txt -o test/output -x 30
``` 

<a name="example-5"></a>
### Example 5 (Optional): generate 100 ARACNe3 subnetworks, dividing the labor between 10 jobs, and then consolidate into one
#### Create the subnetworks
You will have to prepare your job in a way that is specific to your HPC cluster. Assuming you create a Unix loop to submit each job, where the shell variable `JOBID` is unique for each job:
- Set `--runid $JOBID` for each job.
- Use the same output location. 
- If you specify a `--seed`, you should provide a different seed to each job.

```
( ... your scheduler command ... ) /location/of/ARACNe3/build/src/ARACNe3_app_release -e /path/to/exp_mat.txt -r /path/to/regulators.txt -o /common/output/location -x 10 --skip-consolidate --runid $JOBID
``` 

We recommend also setting `--threads` to however many cores each cluster node has, but this is optional.

#### Consolidate the subnetworks
After all your jobs have finished, run ARACNe3 in `--consolidate-mode` to build your final network.

```
( ... your scheduler command ... ) /location/of/ARACNe3/build/src/ARACNe3_app_release -e /path/to/exp_mat.txt -r /path/to/regulators.txt -o /common/output/location -x 100 --consolidate-mode
``` 

---

<a name="input-file-format"></a>
##  Input file format

---

### A gene/regulator list
A text file, containing one symbol per line. E.g.,

```
g_10_
g_10011_
g_1_
```

### Expression file
A normalized expression profile, with genes on rows and samples on columns. This should be in `tsv` format and have both row and column names. For example, the matrix below is a compliant `tsv`:

```
gene	Sample1	Sample2	Sample3
g_1_	4.99	2.93	0.39
g_10_	0.58	0.18	2.65
g_9432_	3.00	1.27	7.63
g_10006_	1.30	0.05	0.68
g_10011_	0.055	0.73	4.64
```

---

##  Contact

---

Please contact Aaron Griffin or Andrew Howe for questions regarding this project.

Aaron T. Griffin - atg2142@cumc.columbia.edu

Andrew R. Howe - arh2207@columbia.edu
