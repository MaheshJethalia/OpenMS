// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry               
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2014.
// 
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution 
//    may be used to endorse or promote products derived from this software 
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS. 
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING 
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// --------------------------------------------------------------------------
// $Maintainer: Clemens Groepl $
// $Authors: Eva Lange, Clemens Groepl $
// --------------------------------------------------------------------------

#include <OpenMS/CONCEPT/ClassTest.h>
#include <OpenMS/test_config.h>
#include <OpenMS/KERNEL/StandardTypes.h>

///////////////////////////
#include <OpenMS/ANALYSIS/MAPMATCHING/PoseClusteringAffineSuperimposer.h>
///////////////////////////

using namespace OpenMS;
using namespace std;

typedef DPosition <2> PositionType;

START_TEST(PoseClusteringAffineSuperimposer, "$Id$")

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

PoseClusteringAffineSuperimposer* ptr = 0;
PoseClusteringAffineSuperimposer* nullPointer = 0;
BaseSuperimposer* base_nullPointer = 0;

START_SECTION((PoseClusteringAffineSuperimposer()))
	ptr = new PoseClusteringAffineSuperimposer();
	TEST_NOT_EQUAL(ptr, nullPointer)
END_SECTION

START_SECTION((virtual ~PoseClusteringAffineSuperimposer()))
	delete ptr;
END_SECTION

START_SECTION((static BaseSuperimposer* create()))
  BaseSuperimposer* base_ptr = 0;
	base_ptr = PoseClusteringAffineSuperimposer::create();
  TEST_NOT_EQUAL(base_ptr, base_nullPointer)
END_SECTION

START_SECTION((static const String getProductName()))
  PoseClusteringAffineSuperimposer pcat;
  
  TEST_EQUAL(pcat.getName() == "poseclustering_affine",true)
END_SECTION

START_SECTION((virtual void run(const ConsensusMap& map_model, const ConsensusMap& map_scene, TransformationDescription& transformation)))
  std::vector<ConsensusMap> input(2);
  Feature feat1;
  Feature feat2;
  PositionType pos1(1,1);
  PositionType pos2(5,5);
  feat1.setPosition(pos1);
  feat1.setIntensity(100.0f);
  feat2.setPosition(pos2);
  feat2.setIntensity(100.0f);
  input[0].push_back(ConsensusFeature(feat1));
  input[0].push_back(ConsensusFeature(feat2));
  
  Feature feat3;
  Feature feat4;
  PositionType pos3(1.4,1.02);
  PositionType pos4(5.4,5.02);
  feat3.setPosition(pos3);
  feat3.setIntensity(100.0f);
  feat4.setPosition(pos4);
  feat4.setIntensity(100.0f);
  input[1].push_back(ConsensusFeature(feat3));
  input[1].push_back(ConsensusFeature(feat4));

  Param parameters;
  parameters.setValue(String("scaling_bucket_size"), 0.01);
  parameters.setValue(String("shift_bucket_size"), 0.1);

  // If hashing goes wrong, get debug output with the following:
  //  parameters.setValue(String("dump_buckets"),"pcast_buckets");
  //  parameters.setValue(String("dump_pairs"),"pcast_pairs");

  TransformationDescription transformation;
  PoseClusteringAffineSuperimposer pcat;
  pcat.setParameters(parameters);

  // That's a precondition for run()!  Now even documented :-)
  input[0].updateRanges();
  input[1].updateRanges();

  pcat.run(input[0], input[1], transformation);

  TEST_STRING_EQUAL(transformation.getModelType(), "linear")
  parameters = transformation.getModelParameters();
	TEST_EQUAL(parameters.size(), 2)    
  TEST_REAL_SIMILAR(parameters.getValue("slope"), 1.0)
  TEST_REAL_SIMILAR(parameters.getValue("intercept"), -0.4)
END_SECTION

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
END_TEST



