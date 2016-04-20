#include "graspGenerationPlugin.h"

#include <boost/foreach.hpp>
#include <cmath>
#include <fstream>

#include <include/world.h>
#include <include/body.h>
#include <include/robot.h>
#include <include/graspitGUI.h>
#include <include/ivmgr.h>

#include <include/EGPlanner/egPlanner.h>
#include <include/EGPlanner/simAnnPlanner.h>
#include <include/EGPlanner/searchState.h>
#include <include/EGPlanner/searchEnergy.h>

#include <include/grasp.h>
#include <include/triangle.h>

#include <cmdline/cmdline.h>
#include "include/dbModelLoader.h"

//// mongo specific headers
//#include <mongocxx/client.hpp>
//#include <mongocxx/instance.hpp>
//#include <mongocxx/uri.hpp>
//#include "mongocxx/collection.hpp"
//#include <mongocxx/result/insert_one.hpp>

//#include <bsoncxx/builder/basic/array.hpp>
//#include <bsoncxx/builder/basic/document.hpp>
//#include <bsoncxx/builder/basic/kvp.hpp>
//#include <bsoncxx/types.hpp>

//// streaming protocol specific
//#include <bsoncxx/builder/stream/document.hpp>
//#include <bsoncxx/json.hpp>
#include <cstdlib>
#include <iostream>

#include <iostream>

//#include <bsoncxx/builder/stream/document.hpp>
//#include <bsoncxx/json.hpp>

//#include <mongocxx/client.hpp>
//#include <mongocxx/instance.hpp>

#include "mongo/client/dbclient.h" // for the driver
//#include "mongo-cxx-driver/src/mongo/client/dbclient.h"
//#include "monetary.h"

using mongo::BSONArray;
using mongo::BSONArrayBuilder;
using mongo::BSONObj;
using mongo::BSONObjBuilder;
using mongo::BSONElement;

GraspGenerationPlugin::GraspGenerationPlugin() :
    mPlanner(NULL),
    plannerStarted(false),
    plannerFinished(false),
    evaluatingGrasps(false),
    num_steps(70000)
{

}

GraspGenerationPlugin::~GraspGenerationPlugin()
{
}


void run() {

}

int GraspGenerationPlugin::init(int argc, char **argv)
{
//    mongo::client::Options opt;
//    mongo::client::initialize(opt);
    mongo::DBClientConnection c;
    mongo::client::initialize();
       try {


           c.connect("localhost");
           std::cout << "connected ok" << std::endl;
       } catch( const mongo::DBException &e ) {
           std::cout << "caught " << e.what() << std::endl;
       }

    BSONObjBuilder b;
    b.append("name", "Joe");
    b.append("age", 33);
    BSONObj p = b.obj();
    c.insert("test.persons", p);

    std::cout << "Still alive! Executing here" << std::endl;

//    mongocxx::instance inst{};
//    mongocxx::client conn{mongocxx::uri{}};

    /*
    mongocxx::client conn{mongocxx::uri{"mongodb://tim:ilovetim@ds013221.mlab.com:13221/robolab"}};*/
//    auto coll = conn["test"]["sampleCollection"];
//    std::cout << "passed here" << std::endl;





//    mongocxx::instance inst{};
//    mongocxx::client conn{mongocxx::uri{}};

//    bsoncxx::builder::stream::document document{};

//    auto collection = conn["testdb"]["testcollection"];
//    document << "hello" << "world";

//    collection.insert_one(document.view());
//    auto cursor = collection.find({});

//    for (auto&& doc : cursor) {
//        std::cout << bsoncxx::to_json(doc) << std::endl;
//    }





    std::cout << "Starting GraspGenerationPlugin: " << std::endl ;
    cmdline::parser *parser = new cmdline::parser();

    parser->add<std::string>("mesh_filepath", 'c', "mesh_filepath",  false);
    parser->add<bool>("render", 'l', "render", false);

    parser->parse(argc, argv);

    if (parser->exist("render"))
    {
        render_it = parser->get<bool>("render");

    }
    else
    {
        render_it = false;
    }

    mesh_filepath = QString::fromStdString(parser->get<std::string>("mesh_filepath"));
    std::cout << "render: " << render_it << "\n" ;
    std::cout << "mesh_filepath: " << mesh_filepath.toStdString().c_str() << "\n" ;

  return 0;
}

//This loop is called over and over again. We do 3 different things
// 1) First step: start the planner
// 2) Middle steps: step the planner
// 3) Last step, save the grasps
int GraspGenerationPlugin::mainLoop()
{
    //start planner
    if (!plannerStarted)
    {
        startPlanner();
    }
    //let planner run.
    else if( (plannerStarted) && !plannerFinished )
    {
        stepPlanner();
    }
    //save grasps
    else if(plannerStarted && plannerFinished && (!evaluatingGrasps))
    {
        uploadResults();
    }

  return 0;
}

void GraspGenerationPlugin::startPlanner()
{
    std::cout << "Starting Planner\n" ;

    //TODO
    //here we need to get the hand and object from the cloud. rather than locally
    QString modelUrl = "http://borneo.cs.columbia.edu/modelnet/vision.cs.princeton.edu/projects/2014/ModelNet/data/pyramid/pyramid_000001463/pyramid_000001463.off";
    DbModelLoader loader;
    loader.loadModelFromUrl(modelUrl);

    std::cout << "FINISHED LOADING DEBUG" << std::endl;
//    graspItGUI->getMainWorld()->importBody("GraspableBody", mesh_filepath);
    //this is fine for now, in the future, we may change this
    graspItGUI->getMainWorld()->importRobot("/home/timchunght/graspit/models/robots/pr2_gripper_2010/pr2_gripper_2010.xml");

    mObject = graspItGUI->getMainWorld()->getGB(0);
    mObject->setMaterial(5);//rubber

    mHand = graspItGUI->getMainWorld()->getCurrentHand();
    mHand->getGrasp()->setObjectNoUpdate(mObject);
    mHand->getGrasp()->setGravity(false);

    mHandObjectState = new GraspPlanningState(mHand);
    mHandObjectState->setObject(mObject);
    mHandObjectState->setPositionType(SPACE_AXIS_ANGLE);
    mHandObjectState->setRefTran(mObject->getTran());
    mHandObjectState->reset();

    mPlanner = new SimAnnPlanner(mHand);
    ((SimAnnPlanner*)mPlanner)->setModelState(mHandObjectState);

    mPlanner->setEnergyType(ENERGY_CONTACT_QUALITY);
    mPlanner->setContactType(CONTACT_PRESET);
    mPlanner->setMaxSteps(num_steps);

    mPlanner->resetPlanner();

    mPlanner->startPlanner();
    plannerStarted = true;
}

void GraspGenerationPlugin::stepPlanner()
{
    if ( mPlanner->getCurrentStep() >= num_steps)
    {
        mPlanner->stopPlanner();
        plannerFinished=true;
    }
}

void GraspGenerationPlugin::uploadResults()
{


    // mongo test code
//    mongocxx::client conn{mongocxx::uri{}};
//    auto coll = conn["test"]["sampleCollection:"];
//    // basic::document builds a BSON document.
//    auto doc = builder::basic::document{};
//    // We append key-value pairs to a document using the kvp helper.
//    doc.append(
//        kvp("foo", "bar"));  // string literal value will be converted to b_utf8 automatically
//    doc.append(kvp("baz", types::b_bool{false}));
//    doc.append(kvp("garply", types::b_double{3.14159}));
//    doc.view();
//    coll.insert_one(doc.view());
//    //mongo test code ends here

    SearchEnergy *mEnergyCalculator = new SearchEnergy();
    mEnergyCalculator->setType(ENERGY_CONTACT_QUALITY);
    mEnergyCalculator->setContactType(CONTACT_PRESET);

    int num_grasps = mPlanner->getListSize();
    std::cout << "Found " << num_grasps << " Grasps. " << std::endl;

    for(int i=0; i < num_grasps; i++)
    {
        GraspPlanningState gps = mPlanner->getGrasp(i);
        gps.execute(mHand);
        mHand->autoGrasp(render_it, 1.0, false);
        bool is_legal;
        double new_planned_energy;

        mEnergyCalculator->analyzeCurrentPosture(mHand,graspItGUI->getMainWorld()->getGB(0),is_legal,new_planned_energy,false );
        gps.setEnergy(new_planned_energy);
        gps.saveCurrentHandState();

        graspItGUI->getIVmgr()->getViewer()->render();
        usleep(1000000);

        double dofVals [mHand->getNumDOF()];
        mHand->getDOFVals(dofVals);

        transf hand_pose = mHand->getPalm()->getTran();




        //TODO
        //here we need to save all this to the database:
        std::cout << "Object: " << mesh_filepath.toStdString().c_str() << std::endl;
        std::cout << "Hand: " << mHand->getFilename().toStdString().c_str() << std::endl;
        std::cout << "Energy TYPE: " << "ENERGY_CONTACT_QUALITY" << std::endl;
        std::cout << "Energy Value: " << new_planned_energy << std::endl;
        std::cout << "Pose: ";
        std::cout << hand_pose.translation().x() << "; ";
        std::cout << hand_pose.translation().y() << "; ";
        std::cout << hand_pose.translation().z() << "; ";
        std::cout << hand_pose.rotation().w << "; ";
        std::cout << hand_pose.rotation().x << "; ";
        std::cout << hand_pose.rotation().y << "; ";
        std::cout << hand_pose.rotation().z << std::endl;
        std::cout << "Dof: ";
        for(int dof_idx = 0; dof_idx < mHand->getNumDOF(); dof_idx ++)
        {
            std::cout << dofVals[dof_idx] << "; ";
        }
        std::cout << std::endl;

    }

    assert(false);
}

