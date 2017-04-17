/*
 * Copyright (C) 2017 by Benedict Paten (benedictpaten@gmail.com) & Arthur Rand (arand@soe.ucsc.edu)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include <getopt.h>
#include <stdio.h>
#include <ctype.h>
#include "stRPHmm.h"
#include "sam.h"

void usage() {
    fprintf(stderr, "marginPhase [options] BAM_FILE\n");
    fprintf(stderr,
            "Phases the reads in an interval of a BAM file reporting a gVCF file "
            "giving genotypes and haplotypes for region.\n");
    fprintf(stderr, "-a --logLevel : Set the log level\n");
    fprintf(stderr, "-h --help : Print this help screen\n");
    fprintf(stderr, "-p --params [FILE_NAME] : File containing parameters for model.\n");
}

stRPHmmParameters *parseParameters(char *paramsFile) {
    /*
     * TODO: Read model parameters from params file
     * See params.json.
     * Suggest writing basic parser / tests and putting in impl/parser.c (or something similar)
     * Suggest using http://zserge.com/jsmn.html for parsing the json
     *
     * Notes:
     *      "alphabet" is an array specifying the conversion of symbols from the read alignments into the non-negative integer space
     *      used by the program.  e.g. "alphabet" : [ "Aa", "Cc", "Gg", "Tt", "-" ] specifies an alphabet of cardinality 5,
     *      with each string in the array specifying which characters map to which integer, starting from 0. In the example,
     *      "C" or a "c" character is mapped to 1 while "-" is mapped to 4.
     *
     *      The wildcard symbols are treated as mapping to each possible integer symbol with equal probability
     *      Any other symbol encountered by the parsing of reads should be treated as an error
     *
     *      If ALPHABET_SIZE does not equal the cardinality of the input alphabet then an error should be thrown.
     *
     *      The "haplotypeSubstitutionModel" gives probabilities of substitutions between haplotypes in the model, the "readErrorModel"
     *      gives the probabilities of errors in the reads.
     *      Each is a square matrix of size alphabet size * alphabet size
     *      Each should be
     *      converted to log space for the model. Each row of each matrix should sum to 1.0 (roughly) and be normalised to 1.0
     *
     */
    return NULL;
}

int main(int argc, char *argv[]) {
    // Parameters / arguments
    //char * logLevelString = NULL;
    char * logLevelString = "debug";
    char *bamFile = NULL;
    char *vcfFile = NULL;
    char *paramsFile = "params.json";



    // Parse the options
    while (1) {
        static struct option long_options[] = {
                { "logLevel", required_argument, 0, 'a' },
                { "help", no_argument, 0, 'h' },
                { "params", required_argument, 0, 'p' },
                { 0, 0, 0, 0 } };

        int option_index = 0;

        int key = getopt_long(argc, argv, "a:hp:", long_options, &option_index);

        if (key == -1) {
            break;
        }

        switch (key) {
        case 'a':
            logLevelString = stString_copy(optarg);
            st_setLogLevelFromString(logLevelString);
            break;
        case 'h':
            usage();
            return 0;
        case 'p':
            paramsFile = stString_copy(optarg);
            break;
        default:
            usage();
            return 1;
        }
    }

    st_setLogLevelFromString(logLevelString);

    st_logInfo("Starting marginPhase...\n");

    // Parse reads for interval
    st_logInfo("Parsing input reads\n");

    stList *profileSequences = NULL;


    samFile *fp_in = hts_open(argv[1],"r"); //open bam file
    bam_hdr_t *bamHdr = sam_hdr_read(fp_in); //read header
    bam1_t *aln = bam_init1(); //initialize an alignment


    /*
     * TODO: Use htslib to parse the reads within an input interval of a reference sequence of a bam file
     * and create a list of profile sequences using
     * stProfileSeq *stProfileSeq_constructEmptyProfile(char *referenceName,
            int64_t referenceStart, int64_t length);
       where for each position you turn the character into a profile probability, as shown in the tests

       In future we can use the mapq scores for reads to adjust these profiles, or for signal level alignments
       use the posterior probabilities.
     */

    // Parse any model parameters
    st_logInfo("Parsing model parameters\n");
    stRPHmmParameters *params = parseParameters(paramsFile);

    // Create HMMs
    st_logInfo("Creating read partitioning HMMs\n");
    stList *hmms = getRPHmms(profileSequences, params);

    // Break up the hmms where the phasing is uncertain
    st_logInfo("Breaking apart HMMs where the phasing is uncertain\n");

    stList *l = stList_construct3(0, (void (*)(void *))stRPHmm_destruct2);
    while(stList_length(hmms) > 0) {
        stList_appendAll(l, stRPHMM_splitWherePhasingIsUncertain(stList_pop(hmms)));
    }
    hmms = l;

    // Create HMM traceback and genome fragments

    // For each read partitioning HMM
    for(int64_t i=0; i<stList_length(hmms); i++) {
        stRPHmm *hmm = stList_get(hmms, i);

        st_logInfo("Creating genome fragment for reference sequence: %s, start: %" PRIi64 ", length: %" PRIi64 "\n",
                    hmm->referenceName, hmm->refStart, hmm->refLength);

        // Run the forward-backward algorithm
        stRPHmm_forwardBackward(hmm);

        // Now compute a high probability path through the hmm
        stList *path = stRPHmm_forwardTraceBack(hmm);

        // Compute the genome fragment
        stGenomeFragment *gF = stGenomeFragment_construct(hmm, path);

        // Write out VCF
        st_logInfo("Writing out VCF for fragment\n");
        /*
         * TODO: Convert the genome fragment into a portion of a VCF file (we'll need to write the header out earlier)
         * We can express the genotypes and (using phase sets) the phasing relationships.
         */

        // Optionally write out two BAMs, one for each read partition
        st_logInfo("Writing out BAM partitions for fragment\n");
        /*
         * TODO: Optionally, write out a new BAM file expressing the partition (which reads belong in which partition)
         * Not sure if we need to write out multiple files or if we can add a per read flag to express this information.
         */
    }

    // Cleanup
    stList_destruct(hmms);

    //while(1); // Use this for testing for memory leaks

    return 0;
}

