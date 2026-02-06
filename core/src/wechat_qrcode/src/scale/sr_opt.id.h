#ifndef NCNN_INCLUDE_GUARD_sr_id_h
#define NCNN_INCLUDE_GUARD_sr_id_h
namespace sr_param_id {
const int LAYER_data = 0;
const int BLOB_data = 0;
const int LAYER_splitncnn_0 = 1;
const int BLOB_data_splitncnn_0 = 1;
const int BLOB_data_splitncnn_1 = 2;
const int LAYER_conv0 = 2;
const int BLOB_conv0_conv0_lrelu = 3;
const int LAYER_splitncnn_1 = 3;
const int BLOB_conv0_conv0_lrelu_splitncnn_0 = 4;
const int BLOB_conv0_conv0_lrelu_splitncnn_1 = 5;
const int LAYER_db1_reduce = 4;
const int BLOB_db1_reduce_db1_reduce_lrelu = 6;
const int LAYER_db1_3x3 = 5;
const int BLOB_db1_3x3_db1_3x3_lrelu = 7;
const int LAYER_db1_1x1 = 6;
const int BLOB_db1_1x1_db1_1x1_lrelu = 8;
const int LAYER_db1_concat = 7;
const int BLOB_db1_concat = 9;
const int LAYER_splitncnn_2 = 8;
const int BLOB_db1_concat_splitncnn_0 = 10;
const int BLOB_db1_concat_splitncnn_1 = 11;
const int LAYER_db2_reduce = 9;
const int BLOB_db2_reduce_db2_reduce_lrelu = 12;
const int LAYER_db2_3x3 = 10;
const int BLOB_db2_3x3_db2_3x3_lrelu = 13;
const int LAYER_db2_1x1 = 11;
const int BLOB_db2_1x1_db2_1x1_lrelu = 14;
const int LAYER_db2_concat = 12;
const int BLOB_db2_concat = 15;
const int LAYER_upsample_reduce = 13;
const int BLOB_upsample_reduce_upsample_reduce_lrelu = 16;
const int LAYER_upsample_deconv = 14;
const int BLOB_upsample_deconv_upsample_lrelu = 17;
const int LAYER_upsample_rec = 15;
const int BLOB_upsample_rec = 18;
const int LAYER_splitncnn_3 = 16;
const int BLOB_upsample_rec_splitncnn_0 = 19;
const int BLOB_upsample_rec_splitncnn_1 = 20;
const int LAYER_nearest = 17;
const int BLOB_nearest = 21;
const int LAYER_Crop1 = 18;
const int BLOB_Crop1 = 22;
const int LAYER_fc = 19;
const int BLOB_fc = 23;
} // namespace sr_param_id
#endif // NCNN_INCLUDE_GUARD_sr_id_h
