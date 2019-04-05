//
// Created by tpesout on 3/29/19.
//

#include "margin.h"
#include "helenFeatures.h"

#ifdef _HDF5
#include <hdf5.h>
#endif


PoaFeatureSimpleWeight *PoaFeature_SimpleWeight_construct(int64_t refPos, int64_t insPos) {
    PoaFeatureSimpleWeight *feature = st_calloc(1, sizeof(PoaFeatureSimpleWeight));
    feature->refPosition = refPos;
    feature->insertPosition = insPos;
    feature->label = '\0';
    feature->nextInsert = NULL;
    return feature;
}

void PoaFeature_SimpleWeight_destruct(PoaFeatureSimpleWeight *feature) {
    if (feature->nextInsert != NULL) {
        PoaFeature_SimpleWeight_destruct(feature->nextInsert);
    }
    free(feature);
}


PoaFeatureRleWeight *PoaFeature_RleWeight_construct(int64_t refPos, int64_t insPos) {
    PoaFeatureRleWeight *feature = st_calloc(1, sizeof(PoaFeatureRleWeight));
    feature->refPosition = refPos;
    feature->insertPosition = insPos;
    feature->labelChar = '\0';
    feature->labelRunLength = 0;
    feature->nextInsert = NULL;
}

void PoaFeature_RleWeight_destruct(PoaFeatureRleWeight *feature) {
    if (feature->nextInsert != NULL) {
        PoaFeature_RleWeight_destruct(feature->nextInsert);
    }
    free(feature);
}

int PoaFeature_SimpleWeight_charIndex(Symbol character, bool forward) {
    int pos = character * 2 + (forward ? POS_STRAND_IDX : NEG_STRAND_IDX);
    assert(pos < POAFEATURE_SIMPLE_WEIGHT_TOTAL_SIZE);
    return pos;
}
int PoaFeature_SimpleWeight_gapIndex(bool forward) {
    int pos = POAFEATURE_SYMBOL_GAP_POS * 2 + (forward ? POS_STRAND_IDX : NEG_STRAND_IDX);
    assert(pos < POAFEATURE_SIMPLE_WEIGHT_TOTAL_SIZE);
    return pos;
}
int PoaFeature_RleWeight_charIndex(Symbol character, int64_t runLength, bool forward) {
    int pos = (character * POAFEATURE_MAX_RUN_LENGTH + runLength) * 2 + (forward ? POS_STRAND_IDX : NEG_STRAND_IDX);
    assert(pos < POAFEATURE_RLE_WEIGHT_TOTAL_SIZE);
    return pos;
}
int PoaFeature_RleWeight_gapIndex(bool forward) {
    int pos = (POAFEATURE_SYMBOL_GAP_POS * POAFEATURE_MAX_RUN_LENGTH) * 2 + (forward ? POS_STRAND_IDX : NEG_STRAND_IDX);
    assert(pos < POAFEATURE_RLE_WEIGHT_TOTAL_SIZE);
    return pos;
}

stList *poa_getSimpleWeightFeatures(Poa *poa, stList *bamChunkReads) {

    // initialize feature list
    stList *featureList = stList_construct3(0, (void (*)(void *)) PoaFeature_SimpleWeight_destruct);
    for(int64_t i=1; i<stList_length(poa->nodes); i++) {
        stList_append(featureList, PoaFeature_SimpleWeight_construct(i - 1, 0));
    }

    // for logging (of errors)
    char *logIdentifier = getLogIdentifier();

    // iterate over all positions
    for(int64_t i=0; i<stList_length(featureList); i++) {

        // get feature and node
        PoaFeatureSimpleWeight* feature = stList_get(featureList, i);
        PoaNode *node = stList_get(poa->nodes, i + 1); //skip the first poa node, as it's always an 'N', so featureIdx and poaIdx are off by one

        // get weights for all bases, strands
        double totalWeight, totalPositiveWeight, totalNegativeWeight;
        double *baseWeights = poaNode_getStrandSpecificBaseWeights(node, bamChunkReads,
                                                                   &totalWeight, &totalPositiveWeight, &totalNegativeWeight);
        for(int64_t j=0; j<SYMBOL_NUMBER; j++) {
            double positiveStrandBaseWeight = baseWeights[j * 2 + POS_STRAND_IDX];
            double negativeStrandBaseWeight = baseWeights[j * 2 + NEG_STRAND_IDX];
            feature->weights[PoaFeature_SimpleWeight_charIndex(j, TRUE)] += positiveStrandBaseWeight;
            feature->weights[PoaFeature_SimpleWeight_charIndex(j, FALSE)] += negativeStrandBaseWeight;
        }
        free(baseWeights);

        // Deletes
        if (stList_length(node->deletes) > 0) {

            // iterate over all deletes
            for (int64_t j = 0; j < stList_length(node->deletes); j++) {
                PoaDelete *delete = stList_get(node->deletes, j);

                // Deletes start AFTER the current position, need to add counts/weights to nodes after the current node
                for (int64_t k = 1; k < delete->length; k++) {
                    if (i + k >= stList_length(poa->nodes)) {
                        st_logInfo(" %s Encountered DELETE that occurs after end of POA!\n", logIdentifier);
                        break;
                    }
                    PoaFeatureSimpleWeight *delFeature = stList_get(featureList, i + k);
                    delFeature->weights[PoaFeature_SimpleWeight_gapIndex(TRUE)] += delete->weightForwardStrand;
                    delFeature->weights[PoaFeature_SimpleWeight_gapIndex(FALSE)] += delete->weightReverseStrand;
                }
            }
        }

        // Inserts
        if (stList_length(node->inserts) > 0) {

            // iterate over all inserts
            for (int64_t j = 0; j < stList_length(node->inserts); j++) {
                PoaInsert *insert = stList_get(node->inserts, j);

                // get feature iterator
                PoaFeatureSimpleWeight *prevFeature = feature;
                for (int64_t k = 0; k < strlen(insert->insert); k++) {
                    // get current feature (or create if necessary)
                    PoaFeatureSimpleWeight *currFeature = prevFeature->nextInsert;
                    if (currFeature == NULL) {
                        currFeature = PoaFeature_SimpleWeight_construct(i, k + 1);
                        prevFeature->nextInsert = currFeature;
                    }

                    Symbol c = symbol_convertCharToSymbol(insert->insert[k]);

                    // add weights
                    currFeature->weights[PoaFeature_SimpleWeight_charIndex(c, TRUE)] += insert->weightForwardStrand;
                    currFeature->weights[PoaFeature_SimpleWeight_charIndex(c, FALSE)] += insert->weightReverseStrand;

                    // iterate
                    prevFeature = currFeature;
                }
            }
        }
    }

    free(logIdentifier);
    return featureList;
}

stList *poa_getWeightedRleFeatures(Poa *poa, stList *bamChunkReads) {

    // initialize feature list
    stList *featureList = stList_construct3(0, (void (*)(void *)) PoaFeature_RleWeight_destruct);
    for(int64_t i=1; i<stList_length(poa->nodes); i++) {
        stList_append(featureList, PoaFeature_RleWeight_construct(i - 1, 0));
    }

    // for logging (of errors)
    char *logIdentifier = getLogIdentifier();

    // iterate over all positions
    for(int64_t i=0; i<stList_length(featureList); i++) {

        // get feature and node
        PoaFeatureRleWeight* feature = stList_get(featureList, i);
        PoaNode *node = stList_get(poa->nodes, i + 1); //skip the first poa node, as it's always an 'N', so featureIdx and poaIdx are off by one

        // get weights for all bases, strands
        double totalWeight, totalPositiveWeight, totalNegativeWeight;
        double *baseWeights = poaNode_getStrandSpecificBaseWeights(node, bamChunkReads,
                                                                   &totalWeight, &totalPositiveWeight, &totalNegativeWeight);
        for(int64_t j=0; j<SYMBOL_NUMBER; j++) {
            double positiveStrandBaseWeight = baseWeights[j * 2 + POS_STRAND_IDX];
            double negativeStrandBaseWeight = baseWeights[j * 2 + NEG_STRAND_IDX];

            //TODO

        }
        free(baseWeights);

        // Deletes
        if (stList_length(node->deletes) > 0) {

            // iterate over all deletes
            for (int64_t j = 0; j < stList_length(node->deletes); j++) {
                PoaDelete *delete = stList_get(node->deletes, j);

                // Deletes start AFTER the current position, need to add counts/weights to nodes after the current node
                for (int64_t k = 1; k < delete->length; k++) {
                    if (i + k >= stList_length(poa->nodes)) {
                        st_logInfo(" %s Encountered DELETE that occurs after end of POA!\n", logIdentifier);
                        break;
                    }
                    PoaFeatureRleWeight *delFeature = stList_get(featureList, i + k);
                    //TODO
                    //delFeature->weights[POAFEATURE_SYMBOL_GAP_POS * 2 + POS_STRAND_IDX] += delete->weightForwardStrand;
                    //delFeature->weights[POAFEATURE_SYMBOL_GAP_POS * 2 + NEG_STRAND_IDX] += delete->weightReverseStrand;
                }
            }
        }

        // Inserts
        if (stList_length(node->inserts) > 0) {

            // iterate over all inserts
            for (int64_t j = 0; j < stList_length(node->inserts); j++) {
                PoaInsert *insert = stList_get(node->inserts, j);

                // get feature iterator
                PoaFeatureRleWeight *prevFeature = feature;
                for (int64_t k = 0; k < strlen(insert->insert); k++) {
                    // get current feature (or create if necessary)
                    PoaFeatureRleWeight *currFeature = prevFeature->nextInsert;
                    if (currFeature == NULL) {
                        currFeature = PoaFeature_RleWeight_construct(i, k + 1);
                        prevFeature->nextInsert = currFeature;
                    }

                    Symbol c = symbol_convertCharToSymbol(insert->insert[k]);

                    // add weights
                    //TODO
                    //currFeature->weights[c * 2 + POS_STRAND_IDX] += insert->weightForwardStrand;
                    //currFeature->weights[c * 2 + NEG_STRAND_IDX] += insert->weightReverseStrand;

                    // iterate
                    prevFeature = currFeature;
                }
            }
        }
    }

    free(logIdentifier);
    return featureList;
}




void poa_annotateSimpleWeightFeaturesWithTruth(stList *features, stList *trueRefAlignment,
                                               RleString *trueRefRleString, int64_t *firstMatchedFeaure,
                                               int64_t *lastMatchedFeature) {
    /*
     Each index in features represents a character in the final consensus string (which in turn was a single node in the
     final POA which was used to generate the consensus).  This consensus was aligned against the true reference (which
     is reflected in the trueRefRleString).  Items in the true refAlignment are stIntTuples with index 0 being the
     alignment weight (discarded), index 1 being the position in the consensus string (and features), and index 2 being
     the position in the trueRefRleString.  So we can iterate over features and the true alignment to assign truth
     labels to each feature.
     */
    static int FEATURE_POS = 1;
    static int REFERENCE_POS = 2;
    *firstMatchedFeaure = -1;
    *lastMatchedFeature = -1;

    // iterate over true ref alignment
    stListIterator *trueRefAlignItor = stList_getIterator(trueRefAlignment);
    stIntTuple *currRefAlign = stList_getNext(trueRefAlignItor);

    // iterate over features
    int64_t trueRefPos = stIntTuple_get(currRefAlign, REFERENCE_POS);
    for (int64_t featureRefPos = 0; featureRefPos < stList_length(features); featureRefPos++) {
        PoaFeatureSimpleWeight *feature = stList_get(features, featureRefPos);
        PoaFeatureSimpleWeight *prevFeature = NULL;

        int64_t featureInsPos = 0;
        while (feature != NULL) {
            // no more ref bases, everything is gaps
            if (currRefAlign == NULL) {
                feature->label = '_';
                feature = feature->nextInsert;
                continue;
            }

            // sanity checks
            assert(stIntTuple_get(currRefAlign, FEATURE_POS) >= featureRefPos && stIntTuple_get(currRefAlign, REFERENCE_POS) >= trueRefPos);

            // match
            if (stIntTuple_get(currRefAlign, FEATURE_POS) == featureRefPos && stIntTuple_get(currRefAlign, REFERENCE_POS) == trueRefPos) {
                feature->label = trueRefRleString->rleString[trueRefPos];
                trueRefPos++;
                currRefAlign = stList_getNext(trueRefAlignItor);
                // handle first and last match
                if (featureInsPos == 0) {
                    if (*firstMatchedFeaure == -1) {
                        *firstMatchedFeaure = featureRefPos;
                    }
                    *lastMatchedFeature = featureRefPos;
                }

            }
                // insert
            else if (trueRefPos < stIntTuple_get(currRefAlign, REFERENCE_POS)) {
                feature->label = trueRefRleString->rleString[trueRefPos];
                trueRefPos++;
            }
                // delete
            else if (featureRefPos < stIntTuple_get(currRefAlign, FEATURE_POS)) {
                feature->label = '_';
            }
                // programmer error
            else {
                st_errAbort("Unhandled case annotating features with true reference characters!\n");
            }

            // always iterate over insert features
            prevFeature = feature;
            feature = feature->nextInsert;
            featureInsPos++;
        }

        // this catches any true inserts which are not present in the poa / feature list
        while (currRefAlign != NULL && featureRefPos < stIntTuple_get(currRefAlign, FEATURE_POS) && trueRefPos < stIntTuple_get(currRefAlign, REFERENCE_POS)) {
            // make new empty feature and save truth
            PoaFeatureSimpleWeight *newFeature = PoaFeature_SimpleWeight_construct(featureRefPos, featureInsPos);
            newFeature->label = trueRefRleString->rleString[trueRefPos];

            // save and iterate
            prevFeature->nextInsert = newFeature;
            prevFeature = newFeature;
            trueRefPos++;
            featureInsPos++;
        }
    }

    stList_destructIterator(trueRefAlignItor);
}

void poa_writeHelenFeatures(HelenFeatureType type, Poa *poa, stList *bamChunkReads, char *outputFileBase,
                            BamChunk *bamChunk, stList *trueRefAlignment, RleString *trueRefRleString) {
    // prep
    stList *features = NULL;
    bool outputLabels = trueRefAlignment != NULL && trueRefRleString != NULL;

    // handle differently based on type
    switch (type) {
        case HFEAT_SIMPLE_WEIGHT :
            // get features
            features = poa_getSimpleWeightFeatures(poa, bamChunkReads);
            int64_t firstMatchedFeature = 0;
            int64_t lastMatchedFeature = stList_length(features) - 1;
            if (outputLabels) {
                poa_annotateSimpleWeightFeaturesWithTruth(features, trueRefAlignment, trueRefRleString,
                                                          &firstMatchedFeature, &lastMatchedFeature);
            }

            writeSimpleWeightHelenFeaturesTSV(outputFileBase, bamChunk, outputLabels, features, type,
                                              firstMatchedFeature, lastMatchedFeature);

            #ifdef _HDF5
            int status = writeSimpleWeightHelenFeaturesHDF5(outputFileBase, bamChunk, outputLabels, features, type,
                                                            firstMatchedFeature, lastMatchedFeature);
            if (status) {
                st_logInfo(" Error writing HELEN features to HDF5 file\n");
            }
            #endif

            break;
        case HFEAT_RLE:
            features = poa_getWeightedRleFeatures(poa, bamChunkReads);

            break;
        default:
            st_errAbort("Unhandled HELEN feature type!\n");
    }

    //cleanup
    stList_destruct(features);
}


void writeSimpleWeightHelenFeaturesTSV(char *outputFileBase, BamChunk *bamChunk, bool outputLabels, stList *features,
                                       HelenFeatureType type, int64_t featureStartIdx, int64_t featureEndIdxInclusive) {

    char *outputFile = stString_print("%s.tsv", outputFileBase);
    FILE *fH = fopen(outputFile, "w");

    // print header
    fprintf(fH, "##contig:%s\n", bamChunk->refSeqName);
    fprintf(fH, "##contigStartPos:%"PRId64"\n", bamChunk->chunkBoundaryStart);
    fprintf(fH, "##contigEndPos:%"PRId64"\n", bamChunk->chunkBoundaryEnd);
    fprintf(fH, "#refPos\tinsPos");
    if (outputLabels) fprintf(fH, "\tlabel");
    for (int64_t i = 0; i < SYMBOL_NUMBER_NO_N; i++) {
        fprintf(fH, "\t%c_fwd\t%c_rev", symbol_convertSymbolToChar((Symbol)i), symbol_convertSymbolToChar((Symbol)i));
    }
    fprintf(fH, "\tgap_fwd\tgap_rev\n");

    // iterate over features
    for (int64_t i = featureStartIdx; i <= featureEndIdxInclusive; i++) {
        PoaFeatureSimpleWeight *feature = stList_get(features, i);

        // iterate over all inserts for each assembly position
        while (feature != NULL) {

            // position and label
            fprintf(fH, "%"PRId64, feature->refPosition);
            fprintf(fH, "\t%"PRId64, feature->insertPosition);
            if (outputLabels) {
                fprintf(fH, "\t%c", feature->label);
            }

            // print weights
            for (int64_t j = 0; j < SYMBOL_NUMBER_NO_N; j++) {
                fprintf(fH, "\t%7.4f", feature->weights[PoaFeature_SimpleWeight_charIndex((Symbol) j, TRUE)] / PAIR_ALIGNMENT_PROB_1);
                fprintf(fH, "\t%7.4f", feature->weights[PoaFeature_SimpleWeight_charIndex((Symbol) j, FALSE)] / PAIR_ALIGNMENT_PROB_1);
            }
            fprintf(fH, "\t%7.4f", feature->weights[PoaFeature_SimpleWeight_gapIndex(TRUE)] / PAIR_ALIGNMENT_PROB_1);
            fprintf(fH, "\t%7.4f\n", feature->weights[PoaFeature_SimpleWeight_gapIndex(FALSE)] / PAIR_ALIGNMENT_PROB_1);

            // iterate
            feature = feature->nextInsert;
        }
    }

    fclose(fH);
    free(outputFile);
}

#ifdef _HDF5
typedef struct {
    int64_t     refPos;
    int64_t     insPos;
} helen_features_position_hdf5_record_t;

typedef struct {
    char        label;
} helen_features_label_hdf5_record_t;

typedef struct {
    double      wAFwd;
    double      wARev;
    double      wCFwd;
    double      wCRev;
    double      wGFwd;
    double      wGRev;
    double      wTFwd;
    double      wTRev;
    double      wGapFwd;
    double      wGapRev;
} helen_features_simple_weight_hdf5_record_t;      /* Compound type */

#define HDF5_FEATURE_SIZE 1000

int writeSimpleWeightHelenFeaturesHDF5(char *outputFileBase, BamChunk *bamChunk, bool outputLabels, stList *features,
                                       HelenFeatureType type, int64_t featureStartIdx, int64_t featureEndIdxInclusive) {

    herr_t      status = 0;

    /*
     * Get feature data set up
     */

    // count features, create feature array
    uint64_t featureCount = 0;
    for (int64_t i = featureStartIdx; i <= featureEndIdxInclusive; i++) {
        PoaFeatureSimpleWeight *feature = stList_get(features, i);
        while (feature != NULL) {
            featureCount++;
            feature = feature->nextInsert;
        }
    }
    if (featureCount < HDF5_FEATURE_SIZE) {
        char *logIdentifier = getLogIdentifier();
        st_logInfo(" %s Feature count %"PRId64" less than minimum of %d", logIdentifier, featureCount, HDF5_FEATURE_SIZE);
        free(logIdentifier);
        return -1;
    }

    // get all feature data into an array
    helen_features_position_hdf5_record_t *positionData =
            st_calloc(featureCount, sizeof(helen_features_simple_weight_hdf5_record_t));
    helen_features_simple_weight_hdf5_record_t *simpleWeightData =
            st_calloc(featureCount, sizeof(helen_features_simple_weight_hdf5_record_t));
    helen_features_label_hdf5_record_t *labelData = NULL;
    if (outputLabels) {
        labelData = st_calloc(featureCount, sizeof(helen_features_simple_weight_hdf5_record_t));
    }

    // add all data to features
    featureCount = 0;
    for (int64_t i = featureStartIdx; i <= featureEndIdxInclusive; i++) {
        PoaFeatureSimpleWeight *feature = stList_get(features, i);
        while (feature != NULL) {
            positionData[featureCount].refPos = feature->refPosition;
            positionData[featureCount].insPos = feature->insertPosition;
            simpleWeightData[featureCount].wAFwd = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('A'), TRUE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wARev = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('A'), FALSE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wCFwd = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('C'), TRUE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wCRev = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('C'), FALSE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wGFwd = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('G'), TRUE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wGRev = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('G'), FALSE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wTFwd = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('T'), TRUE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wTRev = feature->weights[PoaFeature_SimpleWeight_charIndex(symbol_convertCharToSymbol('T'), FALSE)] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wGapFwd = feature->weights[POAFEATURE_SYMBOL_GAP_POS * 2 + POS_STRAND_IDX] / PAIR_ALIGNMENT_PROB_1;
            simpleWeightData[featureCount].wGapRev = feature->weights[POAFEATURE_SYMBOL_GAP_POS * 2 + NEG_STRAND_IDX] / PAIR_ALIGNMENT_PROB_1;
            if (outputLabels) {
                labelData[featureCount].label = feature->label;
            }

            featureCount++;
            feature = feature->nextInsert;
        }
    }


    /*
     * Get hdf5 data set up
     */

    // create datatypes
    hid_t simpleWeightType = H5Tcreate (H5T_COMPOUND, sizeof (helen_features_simple_weight_hdf5_record_t));
    status |= H5Tinsert (simpleWeightType, "a_fwd", HOFFSET (helen_features_simple_weight_hdf5_record_t, wAFwd), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "a_rev", HOFFSET (helen_features_simple_weight_hdf5_record_t, wARev), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "c_fwd", HOFFSET (helen_features_simple_weight_hdf5_record_t, wCFwd), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "c_rev", HOFFSET (helen_features_simple_weight_hdf5_record_t, wCRev), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "g_fwd", HOFFSET (helen_features_simple_weight_hdf5_record_t, wGFwd), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "g_rev", HOFFSET (helen_features_simple_weight_hdf5_record_t, wGRev), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "t_fwd", HOFFSET (helen_features_simple_weight_hdf5_record_t, wTFwd), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "t_rev", HOFFSET (helen_features_simple_weight_hdf5_record_t, wTRev), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "gap_fwd", HOFFSET (helen_features_simple_weight_hdf5_record_t, wGapFwd), H5T_NATIVE_DOUBLE);
    status |= H5Tinsert (simpleWeightType, "gap_rev", HOFFSET (helen_features_simple_weight_hdf5_record_t, wGapRev), H5T_NATIVE_DOUBLE);

    hid_t positionType = H5Tcreate (H5T_COMPOUND, sizeof (helen_features_position_hdf5_record_t));
    status |= H5Tinsert (positionType, "refPos", HOFFSET (helen_features_position_hdf5_record_t, refPos), H5T_NATIVE_INT64);
    status |= H5Tinsert (positionType, "insPos", HOFFSET (helen_features_position_hdf5_record_t, insPos), H5T_NATIVE_INT64);

    hid_t labelType = H5Tcreate (H5T_COMPOUND, sizeof (helen_features_label_hdf5_record_t));
    status |= H5Tinsert (labelType, "label", HOFFSET (helen_features_label_hdf5_record_t, label), H5T_NATIVE_CHAR);


    /*
     * Write features to files
     */

    // each file must have exactly 1000 features
    int64_t totalFeatureFiles = (int64_t) (featureCount / HDF5_FEATURE_SIZE) + (featureCount % HDF5_FEATURE_SIZE == 0 ? 0 : 1);
    int64_t featureOffset = (int64_t) ((HDF5_FEATURE_SIZE * totalFeatureFiles - featureCount) / (int64_t) (featureCount / HDF5_FEATURE_SIZE));

    for (int64_t featureIndex = 0; featureIndex < totalFeatureFiles; featureIndex++) {
        // get start pos
        int64_t chunkFeatureStartIdx = (HDF5_FEATURE_SIZE * featureIndex) - (featureOffset * featureIndex);
        if (featureIndex + 1 == totalFeatureFiles) chunkFeatureStartIdx = featureCount - HDF5_FEATURE_SIZE;

        // create file
        char *outputFile = stString_print("%s.%"PRId64".h5", outputFileBase, featureIndex);
        hid_t file = H5Fcreate (outputFile, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

        hsize_t dimension = HDF5_FEATURE_SIZE;
        hid_t space = H5Screate_simple (1, &dimension, NULL);

        // create and write datasets
        hid_t positionDataset = H5Dcreate (file, "position", positionType, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        hid_t simpleWeightDataset = H5Dcreate (file, "simpleWeight", simpleWeightType, space, H5P_DEFAULT, H5P_DEFAULT,
                                               H5P_DEFAULT);
        status |= H5Dwrite (simpleWeightDataset, simpleWeightType, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                &simpleWeightData[chunkFeatureStartIdx]);
        status |= H5Dwrite (positionDataset, positionType, H5S_ALL, H5S_ALL, H5P_DEFAULT, &positionData[chunkFeatureStartIdx]);

        // if labels, add all these too
        if (outputLabels) {
            hid_t labelDataset = H5Dcreate (file, "label", labelType, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            status |= H5Dwrite (labelDataset, labelType, H5S_ALL, H5S_ALL, H5P_DEFAULT, &labelData[chunkFeatureStartIdx]);
            status |= H5Dclose (labelDataset);
        }

        // cleanup
        status |= H5Dclose (positionDataset);
        status |= H5Dclose (simpleWeightDataset);
        status |= H5Sclose (space);
        status |= H5Fclose (file);
        free(outputFile);
    }

    // cleanup
    status |= H5Tclose (positionType);
    status |= H5Tclose (simpleWeightType);
    status |= H5Tclose (labelType);

    return (int) status;
}
#endif