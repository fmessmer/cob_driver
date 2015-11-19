#include "cob_bms_driver/cob_bms_driver_node.h"

template<int N> void big_endian_to_host(const void* in, void* out);
template<> void big_endian_to_host<1>(const void* in, void* out){ *(uint8_t*)out = *(uint8_t*)in;}
template<> void big_endian_to_host<2>(const void* in, void* out){ *(uint16_t*)out = be16toh(*(uint16_t*)in);}
template<> void big_endian_to_host<4>(const void* in, void* out){ *(uint32_t*)out = be32toh(*(uint32_t*)in);}
template<> void big_endian_to_host<8>(const void* in, void* out){ *(uint64_t*)out = be64toh(*(uint64_t*)in);}

template<typename T> T read_value(const can::Frame &f, uint8_t offset){	//TODO: check if this function is properly used for all cases of parameter types
	T res;
	big_endian_to_host<sizeof(T)>(&f.data[offset], &res);
	return res;
}

CobBmsDriverNode::CobBmsDriverNode()
: nh_priv_("~")
{}

CobBmsDriverNode::~CobBmsDriverNode() {
	socketcan_interface_.shutdown();	
}

bool CobBmsDriverNode::prepare() {
		
	//if(!socketcan_interface_.init(can_device_, false)) {
	//	ROS_ERROR_STREAM("bms_driver initialization failed");
	//	return false;	
	//}
	
	//create listeners for CAN frames
	frame_listener_  = socketcan_interface_.createMsgListener(can::CommInterface::FrameDelegate(this, &CobBmsDriverNode::handleFrames));
	
	getRosParameters();
	
	loadParameterLists();
	
	//initalize parameter list iterators
	param_list1_it_ = param_list1_.begin();
	param_list2_it_ = param_list2_.begin();
	
	updater_.setHardwareID("none"); 
	updater_.add("BMS Diagnostics Updater", this, &CobBmsDriverNode::produceDiagnostics);
	
	return true;
	
}

void CobBmsDriverNode::getRosParameters()	//TODO: set default values
{	 
	//declarations
    XmlRpc::XmlRpcValue diagnostics;
    std::vector <std::string> topics;
    int poll_frequency;
    
    if (!nh_priv_.getParam("topics", topics)) 
    {
		ROS_INFO_STREAM("Did not find \"topics\" on parameter server.");		
	}    
    setTopics(topics);
    
    if (!nh_priv_.getParam("diagnostics", diagnostics)) 
    {
		ROS_INFO_STREAM("Did not find \"diagnostics\" on parameter server.");		
	}
	loadConfigMap(diagnostics, topics);	//TODO: change name from configMap to DiagnosticsMap !!
	
	if (!nh_priv_.getParam("can_device", can_device_)) 
    {
		ROS_INFO_STREAM("Did not find \"can_device\" on parameter server.");		
	}
	
	if (!nh_priv_.getParam("can_id_to_poll", can_id_to_poll_)) 
    {
		ROS_INFO_STREAM("Did not find \"can_id_to_poll\" on parameter server.");	//TODO: set defaults??	
	}
	
	if (!nh_priv_.getParam("poll_frequency", poll_frequency)) 
    {
		ROS_INFO_STREAM("Did not find \"poll_frequency\" on parameter server.");		
	} 
	else 
	{
		//check the validity of poll_frequency and set poll_period_for_two_parameters_in_ms_
		if ((poll_frequency < 0) && (poll_frequency > 40)) 
		{
			ROS_WARN_STREAM("Invalid parameter value: poll_frequency = "<< poll_frequency << ". Setting poll_frequency to 40 Hz.");
			poll_frequency = 40;
		}
		poll_period_for_two_parameters_in_ms_ = ((1/poll_frequency)*2)*1000;
	}
}

void CobBmsDriverNode::loadConfigMap(XmlRpc::XmlRpcValue diagnostics, std::vector<std::string> topics) 
{
	XmlRpc::XmlRpcValue config_l0_array, config_l1_struct, config_l2, config_l3_struct, xdiagnostics, xdiagnostic_elements, xfields, xpair,temp;
    BmsParameters bms_parameters;
    BmsParameter bms_parameter_temp;
    char id;	
    
    config_l0_array = diagnostics;
    
    ROS_ASSERT(config_l0_array.getType() == XmlRpc::XmlRpcValue::TypeArray);  
    //for each id in list of ids
	for (int32_t i = 0; i < config_l0_array.size(); ++i) 
	{	
		ROS_ASSERT(config_l0_array[i].getType() == XmlRpc::XmlRpcValue::TypeStruct);
		config_l1_struct = config_l0_array[i];
		for (XmlRpc::XmlRpcValue::iterator it1=config_l1_struct.begin(); it1!=config_l1_struct.end(); ++it1) 
		{
			config_l2 = it1->second;
			//XmlRpcValue at config_l2 might be an id (TypeInt) or a list of fields (TypeArray)
			if (config_l2.getType()==XmlRpc::XmlRpcValue::TypeInt) 
			{			
				id = static_cast<char>(static_cast<int>(config_l2));
			}
			else if (config_l2.getType()==XmlRpc::XmlRpcValue::TypeArray) 
			{	
				//for each field in field list. NOTE: each field is a parameter
				for(int32_t j=0; j<config_l2.size(); ++j) 
				{
					ROS_ASSERT(config_l2[j].getType()==XmlRpc::XmlRpcValue::TypeStruct);
					config_l3_struct = config_l2[j];
					//for each element in a field
					for (XmlRpc::XmlRpcValue::iterator it3=config_l3_struct.begin(); it3!=config_l3_struct.end(); ++it3) 
					{
						if (it3->first == "name") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeString);
							bms_parameter_temp.name = static_cast<std::string>(it3->second);
						}
						else if (it3->first == "offset") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeInt);
							bms_parameter_temp.offset = static_cast<int>(it3->second);
						}
						else if (it3->first == "len") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeInt);
							bms_parameter_temp.length = static_cast<int>(it3->second);
						}
						else if (it3->first == "is_signed") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeBoolean);
							bms_parameter_temp.is_signed = static_cast<bool>(it3->second);
						}
						else if (it3->first == "factor") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeDouble);
							bms_parameter_temp.factor = static_cast<double>(it3->second);
						}
						else if (it3->first == "unit") 
						{
							ROS_ASSERT(it3->second.getType()==XmlRpc::XmlRpcValue::TypeString);
							bms_parameter_temp.unit = static_cast<std::string>(it3->second);
						} 
						else ROS_ERROR_STREAM("Unexpected Key: " << it3->first);
					}
					
					//check if the parameter is also a topic
					bms_parameter_temp.is_topic = (find(topics.begin(), topics.end(), bms_parameter_temp.name) != topics.end());
					ROS_INFO_STREAM("bms_parameter_temp.is_topic: " << bms_parameter_temp.is_topic);
					
					//bms_parameter_temp is properly filled at this point, so save it in bms_parameters
					bms_parameters.push_back(bms_parameter_temp);
				}
			}
			else ROS_ERROR_STREAM("Config: Expected either XmlRpc Type: " << XmlRpc::XmlRpcValue::TypeArray << " or " << XmlRpc::XmlRpcValue::TypeInt << ". But found: " << config_l2.getType());
		}
		//save bms_parameters in config map for interpreting them later and producing diagnostics as well as publishing data on topics
		config_map_[id] = bms_parameters;
		ROS_INFO_STREAM("Saved "<< bms_parameters.size() << " parameters at ID: " << id );
		//clear bms_parameters for processing next id
		bms_parameters.clear();
	}
}

void CobBmsDriverNode::setTopics(std::vector<std::string> topics) 
{	
	topics_ = topics;
	for (std::vector<std::string>::iterator it_topic = topics_.begin(); it_topic!=topics_.end(); ++it_topic)
	{
		bms_diagnostics_publishers_[*it_topic] = nh_.advertise<std_msgs::Float64> (*it_topic, 100);
		ROS_INFO_STREAM("Created topic: " << *it_topic);
	}	
}

void CobBmsDriverNode::loadParameterLists() 
{
	if (config_map_.empty()) 
	{
		ROS_ERROR("config_map_ is empty! Can not load parameter lists!");
		return;	
	}
		
	if(topics_.empty()) 
	{
		//fill param_list1_ and param_list2_ such that all parameters are polled at equal intervals; accomplished by having parameter lists of same size
		ROS_WARN("Topic list found to be empty. Polling all parameters at equal time intervals");
		bool toggle = true;
		for (ConfigMap::iterator it = config_map_.begin(); it != config_map_.end(); ++it) 
		{
			toggle? param_list1_.push_back(it->first) : param_list2_.push_back(it->first);
			toggle = !toggle;
		}
	}
	else 
	{
		//fill param_list1_ and param_list2_ such that the parameters which are topics are polled faster; accomplished by saving topics in sa separate (shorter) list
		for (ConfigMap::iterator it = config_map_.begin(); it != config_map_.end(); ++it) 
		{
			BmsParameters current_parameter_list = it->second;
			char parameter_can_id = it->first;
			
			for (size_t j=0; j<current_parameter_list.size(); ++j) 
			{
				if (current_parameter_list.at(j).is_topic) 
				{
					param_list1_.push_back(parameter_can_id);
					break; //parameter_can_id needs to be saved only once
				}
				else
				{
					param_list2_.push_back(parameter_can_id);
					break; //parameter_can_id needs to be saved only once
				}
			}
		}
	}
	ROS_INFO_STREAM("Successfully loaded parameters, in param_list1_: "<<param_list1_.size()<<" and in param_list2_: "<<param_list2_.size());
}

//function that polls for two parameters at a time
bool CobBmsDriverNode::pollBmsforParameters(const char first_parameter_id, const char second_parameter_id)
{	
	can::Frame f(can::Header(can_id_to_poll_,false,false,false),4);
	f.data[0] = 0x01;
	f.data[1] = first_parameter_id;
	f.data[2] = 0x01;
	f.data[3] = second_parameter_id;
	
	socketcan_interface_.send(f);
		
	boost::this_thread::sleep_for(boost::chrono::milliseconds(poll_period_for_two_parameters_in_ms_));
	
	return true;

}

void CobBmsDriverNode::pollNextInParamLists()
{
	//return to start if reached the end of parameter lists
	if (param_list1_it_ == param_list1_.end()) param_list1_it_ = param_list1_.begin();
	if (param_list2_it_ == param_list2_.end()) param_list2_it_ = param_list2_.begin();
	
	ROS_INFO_STREAM("polling paramaters at ids: " <<(int)*param_list1_it_ << " and " << (int) *param_list2_it_);
	
	//poll
	pollBmsforParameters(*param_list1_it_,*param_list2_it_); //
	
	//increment iterators for next poll 
	++param_list1_it_;
	++param_list2_it_;
}

//handler for all frames
void CobBmsDriverNode::handleFrames(const can::Frame &f)
{
	//std::string msg = "handling: " + can::tostring(f, true);
	//LOG(msg);
	
	//id to find in config map, TODO: make the following explicit (char only stores a part of int that is f.id)
	char frame_id = f.id; // int to char!! -b-
	BmsParameters bms_parameters;
	
	ConfigMap::iterator config_map_it = config_map_.find(frame_id);
	if (config_map_it==config_map_.end()) return;
		
	bms_parameters = static_cast<BmsParameters>(config_map_it->second);
		
	for (BmsParameters::iterator param = bms_parameters.begin(); param!=bms_parameters.end(); ++param) 
	{
		double data = read_value<int16_t> (f, param->offset) * param->factor;
		stat_.add(param->name, data);
		//LOG(param->name << ": " << data);
		
		if (param->is_topic)
		{
			std::map<std::string, ros::Publisher>::const_iterator it_pub = bms_diagnostics_publishers_.find(param->name);
			if (it_pub != bms_diagnostics_publishers_.end())
			{
				std_msgs::Float64 float_msg;
				float_msg.data = data;
				(it_pub->second).publish(float_msg);
			}
			else 
			{
				ROS_ERROR_STREAM("Could not find a publisher for: " << param->name);
			}
		}
	}	
	//update diagnostics
	updater_.update();
}


void CobBmsDriverNode::produceDiagnostics(diagnostic_updater::DiagnosticStatusWrapper &stat)
{
	stat.values.insert(stat.values.begin(),stat_.values.begin(),stat_.values.end()); 
}

int main(int argc, char **argv) 
{		
	ros::init(argc, argv, "bms_driver_node");
	
	CobBmsDriverNode cob_bms_driver_node;
			
	if (!cob_bms_driver_node.prepare()) return 1;	

	while (cob_bms_driver_node.nh_.ok())
    {	
		cob_bms_driver_node.pollNextInParamLists();		 
		ros::spinOnce();
    }
    
    return 0;	
}
