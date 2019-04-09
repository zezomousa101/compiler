
#include <fstream>
#include <sstream>
#include <iostream>
#include <dirent.h>
#include <vector>
#include <exception>

#include <string>
#include <time.h>
#include <thread>
#include <sstream>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include "opencv2/imgproc/imgproc_c.h"
#include <compiler/crop.h>
#include <sys/stat.h>
#include<thread>
#include<ctime>
using namespace cv;
using namespace dnn;
using namespace std;

// Initialize the parameters
float confThreshold = 0.49; // Confidence threshold
float nmsThreshold = 0.04;  // Non-maximum suppression threshold
int inpWidth = 416;  // Width of network's input image
int inpHeight = 416; // Height of network's input image
std::string OBJECTS_OUTPUT_FILE;

std::string const EMPTY_IMAGE_PATH = "crop/";

int cn = 0;

static vector<string> weightsVector, namesVector, cfgVector;

static vector<Net> loadedNets;

static std::vector<std::vector<std::string>> classes;

struct DirectoryNotFoundException : public exception
{
    DirectoryNotFoundException(std::string &file){
        message = file + " can not open.";
    }
    virtual const char* what()const throw(){
        return message.c_str();
    }
private:
    std::string message;
};

enum struct Flages{
    drawnFlage,
    paperFlage
};

// Remove the bounding boxes with low confidence using non-maxima suppression
void postprocess(Mat& frame, const vector<Mat>& out, int classesIndex, bool &foundFlage, 
Blob objectBox);

// Draw the predicted bounding box
void drawPred(int classId, float conf, int left, int top, 
    int right, int bottom, Mat& frame, int classesIndex, int counter);

// Get the names of the output layers
vector<String> getOutputsNames(const Net& net);

//get data files from thire directories (names, cfgs, weights)
int getDirFiles (string dir, string filesExtention, vector<string> &files);

//detect object in the image using specific weight 
void detectObjects(int netIndex, string imagePath, int classesIndex, bool &found, Blob objectBoxs);

//check string if end with specific string
bool endsWith(const std::string &mainStr, const std::string &toMatch);

//get the name of input file
std::string getFileName(std::string path);

//check if path is file or not 
bool is_file(const char* path);

//run detection on one file
void imageFileProcess(std::string imagePath, Flages imageFlage);

//the main function to classify the image
void runOperation(int end, Flages imageFlage, char *argv[]);

void executeThread(cv::Mat objectsImage, int width, int height, Blob box);

void loadNets();

void loadClasses();



int main(int argc, char* argv[]){

    auto t1 = std::chrono::system_clock::now();
    if(argc < 2){
        std::cout << "you should pass the image path" << std::endl;
        return 1;
    }

    try{
        getDirFiles("weights", ".weights", weightsVector);
        getDirFiles("names", ".names", namesVector);
        getDirFiles("configurations", ".cfg", cfgVector);
    }catch(std::exception &e){
        std::cout << e.what() << std::endl;
        return 0;
    }

    loadClasses();
    loadNets();

    std::cout << argv[1] << std::endl;
    
    if(string(argv[1]) == "-d" || string(argv[1]) == "--drawn")
        runOperation(argc, Flages::drawnFlage, argv);
    else if(string(argv[1]) == "-p" || string(argv[1]) == "--paper")
        runOperation(argc, Flages::paperFlage, argv);
    else
        std::cout << "invalid image type flage" << std::endl;
    auto t2 = std::chrono::system_clock::now();

    std::chrono::duration<double> elapsed_seconds = t2-t1;
    std::time_t end_time = std::chrono::system_clock::to_time_t(t2);

    std::cout << "finished computation at " << std::ctime(&end_time)
              << "elapsed time: " << elapsed_seconds.count() << "s" << std::endl;

    return 0;
}


void runOperation(int end, Flages imageFlage, char *argv[]){
    for(int i =2; i<end; ++i){
        std::cerr << "itration: " <<i<< std::endl;
        if(is_file(argv[i])){
            std::cerr << "file !!!!!!" << std::endl;
            imageFileProcess(argv[i], imageFlage);
        }else{
            std::cerr << "folder !!!!!!" << std::endl;
            std::vector<string> images;
            getDirFiles(argv[i], "jpg",images);
            std::cerr << "imageSize = " <<images.size() << std::endl;
            for(size_t j = 0; j<images.size();++j){
                imageFileProcess(string(argv[i])+"/"+images[j], imageFlage);
            }
        }
       
    }
}

void imageFileProcess(std::string imagePath, Flages imageFlage){
    std::cerr << std::endl << "imagePath: " << imagePath << std::endl;
    ifstream file(imagePath);
    if(file.good()){
        string image = imagePath;
        cv::Mat img = imread(image, cv::IMREAD_GRAYSCALE);
        int fw, fh;
        fw = img.cols;
        fh = img.rows;
        // cv::resize(img, img, cv::Size(fw - (fw*50/100), fh - (fh*50/100)));
        cv::resize(img, img, cv::Size(600, 900));
        // std::cerr << img.cols << "  " << img.rows << std::endl;
        
        CropImage cImg(img, 4);
        
        
        img = cImg.getCropedImage().clone();
        int width = img.cols;
        int height = img.rows;
        std::cerr << width << " " << height << std::endl;
        string cropedPath = "crop/"+getFileName(image) + ".jpg";
        cv::imwrite(cropedPath, img);
        img.release();
        OBJECTS_OUTPUT_FILE = getFileName(image) + ".viw";
        std::ofstream sFile;
        sFile.open(OBJECTS_OUTPUT_FILE);
        sFile << width << std::endl;
        sFile << height << std::endl;
        sFile.close();
        bool foundFlage = false;
        Blob nullBlob;
        if(imageFlage == Flages::paperFlage){
            for(size_t i =0; i<weightsVector.size();++i){
                // vector<string> classes;
                detectObjects(i, cropedPath, i, foundFlage, nullBlob);
                //std::cerr << namesVector[i] << "\t" << cfgVector[i] << "\t" <<weightsVector[i]<<endl;
            }
        }else if(imageFlage == Flages::drawnFlage){
            
            cv::Mat objectsImage = cv::imread(cropedPath, cv::IMREAD_GRAYSCALE);
            CropImage cObjects(objectsImage, 0);
            vector<Blob> objectsBoxs;
            cObjects.getOjectsCoordinates(objectsBoxs);
            
            // std::thread *imgThreads = new std::thread[objectsBoxs.size()];

            for(size_t i =0; i<objectsBoxs.size();++i){
                // bool *pff = false;
                std::cerr << "starting thread number: " << i << std::endl;
                executeThread(objectsImage, width, height, objectsBoxs[i]);
                // *(imgThreads+i) = std::thread(executeThread, objectsImage, width, height, objectsBoxs[i]);
            }
            // for(size_t i =0; i<objectsBoxs.size();++i){
            //     (imgThreads+i)->join();
            // }
            // delete [] imgThreads;
            cv::imwrite(cropedPath, objectsImage);   
        }
    }else{
        std::cout << imagePath << " file not found." << std::endl;
    }
}

// std::mutex mut;

void executeThread(cv::Mat objectsImage, int width, int height, Blob box){
    // std::lock_guard<std::mutex> lock(mut);
    cv::Mat emptyImage(height, width, CV_8UC1, Scalar(255, 255, 255));
    std::cerr << box.boundingRect << std::endl;
    cv::Mat obj = objectsImage(box.boundingRect);
    obj.copyTo(emptyImage(cv::Rect( box.boundingRect.x,  box.boundingRect.y, obj.cols, obj.rows)));
    auto id = std::this_thread::get_id();
    stringstream ss;
    ss << id;
    std::string threadEmptyPath = EMPTY_IMAGE_PATH + ss.str() + ".jpg";
    std::cerr << "empty image path = " << threadEmptyPath << std::endl;
    cv::imwrite(threadEmptyPath, emptyImage);
    bool ff = false;
    for(size_t j =0; j<weightsVector.size();++j){
        // vector<string> classes;
        // std::cerr << "current thread id = " << std::this_thread::get_id() << std::endl;
        detectObjects(j, threadEmptyPath, j, ff, box);
        // if(ff)
        //     break;
        //std::cerr << namesVector[i] << "\t" << cfgVector[i] << "\t" <<weightsVector[i]<<endl;
    }
    cv::rectangle(objectsImage, box.boundingRect, Scalar(255, 0, 0), 2);
    cv::circle(objectsImage, box.centerPosition, 3, Scalar(0, 255, 0), -1);

}

bool is_file(const char* path) {
    struct stat buf;
    stat(path, &buf);
    return S_ISREG(buf.st_mode);
}

bool endsWith(const std::string &mainStr, const std::string &toMatch)
{
	if(mainStr.size() >= toMatch.size() &&
			mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
			return true;
		else
			return false;
}

int getDirFiles (string dir, string filesExtention, vector<string> &files){
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(dir.c_str())) == NULL) {
        throw DirectoryNotFoundException(dir);
    }

    while ((dirp = readdir(dp)) != NULL) {
    	// if(endsWith(dirp->d_name, filesExtention))
        if(!(dirp->d_name == std::string(".") || dirp->d_name == string("..")))
            files.push_back(string(dirp->d_name));
    }
    closedir(dp);
    return 0;
}

void loadClasses(){
    for(size_t i = 0; i<weightsVector.size();++i){
        // Load names of classes
        string classesFile = "names/"+namesVector[i];
        ifstream ifs(classesFile.c_str());
        string line;
        std::vector<std::string> temp;
        while (getline(ifs, line)) temp.push_back(line);
        classes.push_back(temp);
    }
}

 void loadNets(){
     for(size_t i =0; i<weightsVector.size();++i){
        // Give the configuration and weight files for the model
        String modelConfiguration = "configurations/"+cfgVector[i];
        String modelWeights = "weights/"+weightsVector[i];

        std::cerr << modelConfiguration << "\t" << modelWeights << std::endl;

        // Load the network
        Net net = readNetFromDarknet(modelConfiguration, modelWeights);
        net.setPreferableBackend(DNN_BACKEND_OPENCV);
        net.setPreferableTarget(DNN_TARGET_CPU);

        loadedNets.push_back(net);
     }
     
 }

void detectObjects(int netIndex, string imagePath, int classesIndex, bool &found, Blob objectBox){
        // std::cerr << "current thread id = " << std::this_thread::get_id() << std::endl;
    
    // Open a video file or an image file or a camera stream.
    string str, outputFile;
     VideoCapture cap;
    // VideoWriter video;
    Mat frame, blob;
    // Net net = loadedNets[netIndex];
    
    try {
        // Open the image file
        str = imagePath;
        ifstream ifile(str);
        if (!ifile) throw("error");
        cap.open(str);
        //str.replace(str.end()-4, str.end());
        outputFile =  "temp/" + OBJECTS_OUTPUT_FILE+std::to_string(cn++)+".jpg";
        
    }catch(exception &ex) {
        cout << ex.what() << endl;
        return;
    }
    

        cap >> frame;

        // Create a 4D blob from a frame.
        
        blobFromImage(frame, blob, 1/255.0, cvSize(inpWidth, inpHeight), Scalar(0,0,0), true, false);
        
        //Sets the input to the network
        loadedNets[netIndex].setInput(blob);
        
        
        // Runs the forward pass to get output of the output layers
        vector<Mat> outs;
        loadedNets[netIndex].forward(outs, getOutputsNames(loadedNets[netIndex]));
        
       
        // Remove the bounding boxes with low confidence
        postprocess(frame, outs, classesIndex, found, objectBox);
        
        // Put efficiency information. The function getPerfProfile returns the overall time for inference(t) and the timings for each of the layers(in layersTimes)
        vector<double> layersTimes;
        double freq = getTickFrequency() / 1000;
        double t = loadedNets[netIndex].getPerfProfile(layersTimes) / freq;
        string label = format("Inference time for a frame : %.2f ms", t);
        putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 255));
        
    cap.release();
}

// Remove the bounding boxes with low confidence using non-maxima suppression
void postprocess(Mat& frame, const vector<Mat>& outs, int classesIndex, bool &foundFlage, 
Blob objectBox){

    vector<int> classIds;
    vector<float> confidences;
    vector<Rect> boxes;
    
    for (size_t i = 0; i < outs.size(); ++i){
        // Scan through all the bounding boxes output from the network and keep only the
        // ones with high confidence scores. Assign the box's class label as the class
        // with the highest score for the box.
        float* data = (float*)outs[i].data;
        for (int j = 0; j < outs[i].rows; ++j, data += outs[i].cols){
            Mat scores = outs[i].row(j).colRange(5, outs[i].cols);
            Point classIdPoint;
            double confidence;
            // Get the value and location of the maximum score
            cv::minMaxLoc(scores, 0, &confidence, 0, &classIdPoint);
             if (confidence > confThreshold){
                foundFlage = true;
            	std::cout<<"confidence: "<<confidence<<"\t class: "<<classIdPoint<<endl;
                int centerX = (int)(data[0] * frame.cols);
                int centerY = (int)(data[1] * frame.rows);
                int width = (int)(data[2] * frame.cols);
                int height = (int)(data[3] * frame.rows);
                int left = centerX - width / 2;
                int top = centerY - height / 2;
                
                classIds.push_back(classIdPoint.x);
                confidences.push_back((float)confidence);
                boxes.push_back(Rect(left, top, width, height));
            }
        }
    }
    
    // Perform non maximum suppression to eliminate redundant overlapping boxes with
    // lower confidences

    vector<int> indices;
    NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);
    
    for (size_t i = 0; i < indices.size(); ++i)
    {
        int idx = indices[i];
        Rect box = boxes[idx];
        if(objectBox.boundingRect.area() > 0){
            drawPred(classIds[idx], confidences[idx], objectBox.boundingRect.x, objectBox.boundingRect.y,
                 objectBox.boundingRect.x + objectBox.boundingRect.width, objectBox.boundingRect.y + objectBox.boundingRect.height,
                  frame, classesIndex, i+1);
        }else{
             drawPred(classIds[idx], confidences[idx], box.x, box.y,
                 box.x + box.width, box.y + box.height, frame, classesIndex, i+1);
        }
       
    }
}

// Draw the predicted bounding box
void drawPred(int classId, float conf, int left, int top, 
    int right, int bottom, Mat& frame, int classesIndex, int counter){
    //Draw a rectangle displaying the bounding box
    rectangle(frame, Point(left, top), Point(right, bottom), Scalar(255, 178, 50), 3);

    std::ofstream file;
    file.open(OBJECTS_OUTPUT_FILE, ios::app);
    file << classes[classesIndex][classId] <<std::endl;
    file << classes[classesIndex][classId]<<"_"<<counter<<std::endl;
    file << left << std::endl;
    file << top << std::endl;
    file << right << std::endl;
    file << bottom << std::endl;
    //file << conf <<std::endl;
    file << "-----------------------------------"<<std::endl;
    file.close();
    
    //Get the label for the class name and its confidence
    string label = format("%.2f", conf);
    if (!classes[classesIndex].empty())
    {
        CV_Assert(classId < (int)classes[classesIndex].size());
        label = classes[classesIndex][classId] + ":" + label;
    }
    
    //Display the label at the top of the bounding box
    int baseLine;
    Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
    top = max(top, labelSize.height);
    rectangle(frame, Point(left, top - round(1.5*labelSize.height)), Point(left + round(1.5*labelSize.width), top + baseLine), Scalar(255, 255, 255), FILLED);
    putText(frame, label, Point(left, top), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,0),1);
    std::string outputFile =  "temp/" + OBJECTS_OUTPUT_FILE+std::to_string(cn++)+".jpg";
     // Write the frame with the detection boxes
    Mat detectedFrame;
    frame.convertTo(detectedFrame, CV_8U);
    imwrite(outputFile, detectedFrame);
}

// Get the names of the output layers
vector<String> getOutputsNames(const Net& net)
{
    static vector<String> names;
    if (names.empty())
    {
        //Get the indices of the output layers, i.e. the layers with unconnected outputs
        vector<int> outLayers = net.getUnconnectedOutLayers();
        
        //get the names of all the layers in the network
        vector<String> layersNames = net.getLayerNames();
        
        // Get the names of the output layers in names
        names.resize(outLayers.size());
        for (size_t i = 0; i < outLayers.size(); ++i)
        names[i] = layersNames[outLayers[i] - 1];
    }
    return names;
}

std::string getFileName(std::string path){
    char slash = '/';
    char dot = '.';
#ifdef _WIN32
    slash = '\\';
#endif
    int last;
    last = path.find_last_of(slash);
    std::string name;
    if(last != string::npos){
        name = path.substr(last+1);
    }else{
        name = path;
    }

    last = name.find_last_of(dot);
    if(last != string::npos){
        name = name.substr(0, last);
    }
    return name;
    
}
