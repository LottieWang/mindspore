/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "common/common.h"
#include "include/api/types.h"
#include "utils/log_adapter.h"

#include "minddata/dataset/include/dataset/audio.h"
#include "minddata/dataset/include/dataset/datasets.h"
#include "minddata/dataset/include/dataset/execute.h"
#include "minddata/dataset/include/dataset/transforms.h"

using namespace mindspore::dataset;
using mindspore::LogStream;
using mindspore::ExceptionType::NoExceptionType;
using mindspore::MsLogLevel::INFO;
using namespace std;

class MindDataTestPipeline : public UT::DatasetOpTesting {
 protected:
};

TEST_F(MindDataTestPipeline, TestAmplitudeToDBPipeline) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto amplitude_to_db_op = audio::AmplitudeToDB();

  ds = ds->Map({amplitude_to_db_op});
  EXPECT_NE(ds, nullptr);

  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, TestAmplitudeToDBWrongArgs) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto amplitude_to_db_op = audio::AmplitudeToDB(ScaleType::kPower, 1.0, -1e-10, 80.0);

  ds = ds->Map({amplitude_to_db_op});
  EXPECT_NE(ds, nullptr);

  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  // Expect failure
  EXPECT_EQ(iter, nullptr);
}

TEST_F(MindDataTestPipeline, Level0_TestBandBiquad001) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto BandBiquadOp = audio::BandBiquad(44100, 200.0);

  ds = ds->Map({BandBiquadOp});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bandbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, Level0_TestBandBiquad002) {
  MS_LOG(INFO) << "Wrong Arg.";
  std::shared_ptr<SchemaObj> schema = Schema();
  // Original waveform
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  std::shared_ptr<Dataset> ds01;
  std::shared_ptr<Dataset> ds02;
  EXPECT_NE(ds, nullptr);

  // Check sample_rate
  MS_LOG(INFO) << "sample_rate is zero.";
  auto band_biquad_op_01 = audio::BandBiquad(0, 200);
  ds01 = ds->Map({band_biquad_op_01});
  EXPECT_NE(ds01, nullptr);

  std::shared_ptr<Iterator> iter01 = ds01->CreateIterator();
  EXPECT_EQ(iter01, nullptr);

  // Check Q_
  MS_LOG(INFO) << "Q_ is zero.";
  auto band_biquad_op_02 = audio::BandBiquad(44100, 200, 0);
  ds02 = ds->Map({band_biquad_op_02});
  EXPECT_NE(ds02, nullptr);

  std::shared_ptr<Iterator> iter02 = ds02->CreateIterator();
  EXPECT_EQ(iter02, nullptr);
}

TEST_F(MindDataTestPipeline, Level0_TestAllpassBiquad001) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto AllpassBiquadOp = audio::AllpassBiquad(44100, 200.0);

  ds = ds->Map({AllpassBiquadOp});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by allpassbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, Level0_TestAllpassBiquad002) {
  MS_LOG(INFO) << "Wrong Arg.";
  std::shared_ptr<SchemaObj> schema = Schema();
  // Original waveform
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  std::shared_ptr<Dataset> ds01;
  std::shared_ptr<Dataset> ds02;
  EXPECT_NE(ds, nullptr);

  // Check sample_rate
  MS_LOG(INFO) << "Sample_rate_ is zero.";
  auto allpass_biquad_op_01 = audio::AllpassBiquad(0, 200.0, 0.707);
  ds01 = ds->Map({allpass_biquad_op_01});
  EXPECT_NE(ds01, nullptr);

  std::shared_ptr<Iterator> iter01 = ds01->CreateIterator();
  EXPECT_EQ(iter01, nullptr);

  // Check Q_
  MS_LOG(INFO) << "Q_ is zero.";
  auto allpass_biquad_op_02 = audio::AllpassBiquad(44100, 200, 0);
  ds02 = ds->Map({allpass_biquad_op_02});
  EXPECT_NE(ds02, nullptr);

  std::shared_ptr<Iterator> iter02 = ds02->CreateIterator();
  EXPECT_EQ(iter02, nullptr);
}

TEST_F(MindDataTestPipeline, Level0_TestBandpassBiquad001) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto BandpassBiquadOp = audio::BandpassBiquad(44100, 200.0);

  ds = ds->Map({BandpassBiquadOp});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bandpassbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, Level0_TestBandpassBiquad002) {
  MS_LOG(INFO) << "Wrong Arg.";
  std::shared_ptr<SchemaObj> schema = Schema();
  // Original waveform
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  std::shared_ptr<Dataset> ds01;
  std::shared_ptr<Dataset> ds02;
  EXPECT_NE(ds, nullptr);

  // Check sample_rate
  MS_LOG(INFO) << "sample_rate is zero.";
  auto bandpass_biquad_op_01 = audio::BandpassBiquad(0, 200);
  ds01 = ds->Map({bandpass_biquad_op_01});
  EXPECT_NE(ds01, nullptr);

  std::shared_ptr<Iterator> iter01 = ds01->CreateIterator();
  EXPECT_EQ(iter01, nullptr);

  // Check Q_
  MS_LOG(INFO) << "Q_ is zero.";
  auto bandpass_biquad_op_02 = audio::BandpassBiquad(44100, 200, 0);
  ds02 = ds->Map({bandpass_biquad_op_02});
  EXPECT_NE(ds02, nullptr);

  std::shared_ptr<Iterator> iter02 = ds02->CreateIterator();
  EXPECT_EQ(iter02, nullptr);
}

TEST_F(MindDataTestPipeline, Level0_TestBandrejectBiquad001) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto BandrejectBiquadOp = audio::BandrejectBiquad(44100, 200.0);

  ds = ds->Map({BandrejectBiquadOp});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bandrejectbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, Level0_TestBandrejectBiquad002) {
  MS_LOG(INFO) << "Wrong Arg.";
  std::shared_ptr<SchemaObj> schema = Schema();
  // Original waveform
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  std::shared_ptr<Dataset> ds01;
  std::shared_ptr<Dataset> ds02;
  EXPECT_NE(ds, nullptr);

  // Check sample_rate
  MS_LOG(INFO) << "sample_rate is zero.";
  auto bandreject_biquad_op_01 = audio::BandrejectBiquad(0, 200);
  ds01 = ds->Map({bandreject_biquad_op_01});
  EXPECT_NE(ds01, nullptr);

  std::shared_ptr<Iterator> iter01 = ds01->CreateIterator();
  EXPECT_EQ(iter01, nullptr);

  // Check Q_
  MS_LOG(INFO) << "Q_ is zero.";
  auto bandreject_biquad_op_02 = audio::BandrejectBiquad(44100, 200, 0);
  ds02 = ds->Map({bandreject_biquad_op_02});
  EXPECT_NE(ds02, nullptr);

  std::shared_ptr<Iterator> iter02 = ds02->CreateIterator();
  EXPECT_EQ(iter02, nullptr);
}

TEST_F(MindDataTestPipeline, Level0_TestBassBiquad001) {
  MS_LOG(INFO) << "Basic Function Test";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto BassBiquadOp = audio::BassBiquad(44100, 50, 200.0);

  ds = ds->Map({BassBiquadOp});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bassbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, Level0_TestBassBiquad002) {
  MS_LOG(INFO) << "Wrong Arg.";
  std::shared_ptr<SchemaObj> schema = Schema();
  // Original waveform
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  std::shared_ptr<Dataset> ds01;
  std::shared_ptr<Dataset> ds02;
  EXPECT_NE(ds, nullptr);

  // Check sample_rate
  MS_LOG(INFO) << "sample_rate is zero.";
  auto bass_biquad_op_01 = audio::BassBiquad(0, 50, 200.0);
  ds01 = ds->Map({bass_biquad_op_01});
  EXPECT_NE(ds01, nullptr);

  std::shared_ptr<Iterator> iter01 = ds01->CreateIterator();
  EXPECT_EQ(iter01, nullptr);

  // Check Q_
  MS_LOG(INFO) << "Q_ is zero.";
  auto bass_biquad_op_02 = audio::BassBiquad(44100, 50, 200.0, 0);
  ds02 = ds->Map({bass_biquad_op_02});
  EXPECT_NE(ds02, nullptr);

  std::shared_ptr<Iterator> iter02 = ds02->CreateIterator();
  EXPECT_EQ(iter02, nullptr);
}

TEST_F(MindDataTestPipeline, TestAnglePipeline) {
  MS_LOG(INFO) << "Doing MindDataTestPipeline-TestAnglePipeline";

  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("complex", mindspore::DataType::kNumberTypeFloat32, {2, 2}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto angle_op = audio::Angle();

  ds = ds->Map({angle_op});
  EXPECT_NE(ds, nullptr);

  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {2};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["complex"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 1);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, TestAnglePipelineError) {
  MS_LOG(INFO) << "Doing MindDataTestPipeline-TestAnglePipelineError";

  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("complex", mindspore::DataType::kNumberTypeFloat32, {3, 2, 1}));
  std::shared_ptr<Dataset> ds = RandomData(4, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto angle_op = audio::Angle();

  ds = ds->Map({angle_op});
  EXPECT_NE(ds, nullptr);

  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  std::unordered_map<std::string, mindspore::MSTensor> row;
  EXPECT_ERROR(iter->GetNextRow(&row));
}

TEST_F(MindDataTestPipeline, TestFrequencyMaskingPipeline) {
  MS_LOG(INFO) << "Doing TestFrequencyMasking Pipeline.";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {200, 200}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto frequencymasking = audio::FrequencyMasking(true, 6);

  ds = ds->Map({frequencymasking});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bandbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  EXPECT_NE(ds, nullptr);

  std::unordered_map<std::string, mindspore::MSTensor> row;
  ASSERT_OK(iter->GetNextRow(&row));

  std::vector<int64_t> expected = {200, 200};

  int i = 0;
  while (row.size() != 0) {
    auto col = row["inputData"];
    ASSERT_EQ(col.Shape(), expected);
    ASSERT_EQ(col.Shape().size(), 2);
    ASSERT_EQ(col.DataType(), mindspore::DataType::kNumberTypeFloat32);
    ASSERT_OK(iter->GetNextRow(&row));
    i++;
  }
  EXPECT_EQ(i, 50);

  iter->Stop();
}

TEST_F(MindDataTestPipeline, TestFrequencyMaskingWrongArgs) {
  MS_LOG(INFO) << "Doing TestFrequencyMasking with wrong args.";
  // Original waveform
  std::shared_ptr<SchemaObj> schema = Schema();
  ASSERT_OK(schema->add_column("inputData", mindspore::DataType::kNumberTypeFloat32, {20, 20}));
  std::shared_ptr<Dataset> ds = RandomData(50, schema);
  EXPECT_NE(ds, nullptr);

  ds = ds->SetNumWorkers(4);
  EXPECT_NE(ds, nullptr);

  auto frequencymasking = audio::FrequencyMasking(true, -100);

  ds = ds->Map({frequencymasking});
  EXPECT_NE(ds, nullptr);

  // Filtered waveform by bandbiquad
  std::shared_ptr<Iterator> iter = ds->CreateIterator();
  // Expect failure
  EXPECT_EQ(iter, nullptr);
}
