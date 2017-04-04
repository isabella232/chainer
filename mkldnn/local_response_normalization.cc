#include <glog/logging.h>
#include <iostream>
#include "common.h"
#include "mkldnn.hpp"
#include "local_response_normalization.h"
#include "utils.h"

using namespace mkldnn;

// extern engine cpu_engine;

template<typename T>
LocalResponseNormalization<T>::LocalResponseNormalization(int n, double k, double alpha, double beta,mkldnn::algorithm alg_kind)
: user_x_mem_(NULL), user_y_mem_(NULL), x_md_(NULL), y_md_(NULL)
			   , gx_mem_(NULL), gy_mem_(NULL), workspace_memory_(NULL)
               , lrn_fwd_desc_(NULL), lrn_fwd_pd_(NULL)
               , lrn_fwd_(NULL), fwd_stream_(NULL)
               , lrn_diff_src_mem_(NULL), lrn_diff_dst_mem_(NULL)
               , lrn_bwd_desc_(NULL), lrn_bwd_pd_(NULL)
               , lrn_bwd_(NULL), bwd_stream_(NULL)
{
	// google::ShutdownGoogleLogging();
	// google::SetLogDestination(google::GLOG_INFO,"./lrnMyInfo");
    // google::LogToStderr();      
	// LOG(INFO) << "n = " << n << " k = " << k << " alpha = " << alpha << "beta = " << beta ;
	p.alpha = alpha;
	p.beta = beta;
	p.aprop_kind = prop_kind::forward_training;
	p.local_size = n;
	p.k = k;
	p.data_format = memory::format::nchw;
	p.diff_data_format = memory::format::any;
	p.aalgorithm = algorithm::lrn_across_channels;
	p.aalgorithm = alg_kind;

	eng.reset(new engine(engine::kind::cpu, 0));
}

template<typename T>
LocalResponseNormalization<T>::~LocalResponseNormalization()
{

}

template<typename T>
int LocalResponseNormalization<T>::forward_setup(
	T* x, int x_d1, int x_d2, int x_d3, int x_d4,
	T* y, int y_d1, int y_d2, int y_d3, int y_d4)
{
	// LOG(INFO) << "forward_setup";
	// LOG(INFO) << "lrn_src_tz "<< x_d1 << x_d2<< x_d3 << x_d4 ;
	// LOG(INFO) << "lrn_dst_tz "<< y_d1 << y_d2<< y_d3 << y_d4 ;
	memory::dims lrn_src_tz = {x_d1, x_d2, x_d3, x_d4};
    memory::dims lrn_dst_tz = {y_d1, y_d2, y_d3, y_d4};

	/* create memory for user data */
	LOG(INFO) << "create memory for user data";
    user_x_mem_.reset(new memory({{{lrn_src_tz}, memory_data_type<T>(),p.data_format}, *eng}, x));
    x_md_.reset(new memory::desc({lrn_src_tz}, memory_data_type<T>(),p.diff_data_format));


    user_y_mem_.reset(new memory({{{lrn_dst_tz}, memory_data_type<T>(),p.data_format}, *eng}, y));
    y_md_.reset(new memory::desc({lrn_dst_tz}, memory_data_type<T>(),p.diff_data_format));


    LOG(INFO) << "lrn_fwd_desc_";
    lrn_fwd_desc_.reset(new lrn_forward::desc(p.aprop_kind, p.aalgorithm, *x_md_,
	    p.local_size, p.alpha, p.beta, p.k));
    lrn_fwd_pd_.reset(new lrn_forward::primitive_desc(*lrn_fwd_desc_, *eng));

  	x_mem_ = user_x_mem_;
    y_mem_ = user_y_mem_;
    // y_mem_.reset(new memory(lrn_fwd_pd_.get()->dst_primitive_desc()));
    bool reorder_x_p = false;
    bool reorder_y_p = false;


    if (memory::primitive_desc(lrn_fwd_pd_.get()->src_primitive_desc())
        != user_x_mem_->get_primitive_desc()) {
        x_mem_.reset(new memory(lrn_fwd_pd_.get()->src_primitive_desc()));
        reorder_x_ = reorder(*user_x_mem_, *x_mem_);
        reorder_x_p = true;
    }

    if (memory::primitive_desc(lrn_fwd_pd_.get()->dst_primitive_desc())
        != user_y_mem_->get_primitive_desc()) {
        y_mem_.reset(new memory(lrn_fwd_pd_.get()->dst_primitive_desc()));
        reorder_y_ = reorder(*y_mem_, *user_y_mem_);
        reorder_y_p = true;
    }

    // LOG(INFO) << "workspace_primitive_desc";
    workspace_memory_.reset(new memory(lrn_fwd_pd_->workspace_primitive_desc()));
    
    // LOG(INFO) << "lrn_fwd_";
    lrn_fwd_.reset(new lrn_forward(*lrn_fwd_pd_, *x_mem_, *workspace_memory_, *y_mem_));

    LOG(INFO) << "    reorder_src: " << reorder_x_p;
    LOG(INFO) << "    reorder_dst: " << reorder_y_p;
 
 	if (reorder_x_p) this->fwd_primitives_.push_back(reorder_x_);
    fwd_primitives_.push_back(*lrn_fwd_);
    if (reorder_y_p) this->fwd_primitives_.push_back(reorder_y_);
    fwd_stream_.reset(new stream(stream::kind::eager));

    return 0;
}

template<typename T>
void LocalResponseNormalization<T>::fwd_reset_mem(T* x,T* y,T* ws)
{
    // LOG(INFO) << "x " << x << "y " << y << "ws " << ws;
    user_x_mem_->set_data_handle(x);
    user_y_mem_->set_data_handle(y);
    workspace_memory_ -> set_data_handle(ws);
}

template<typename T>
int LocalResponseNormalization<T>::forward(
	T* x, int x_d1, int x_d2, int x_d3, int x_d4,
	T* y, int y_d1, int y_d2, int y_d3, int y_d4,
	T* ws, int ws_d1, int ws_d2, int ws_d3, int ws_d4)
{
    if (!fwd_stream_) 
    {
    	// LOG(INFO) << "!fwd_stream_";
        forward_setup(x, x_d1, x_d2, x_d3, x_d4, 
        			  y, y_d1, y_d2, y_d3, y_d4);
        fwd_reset_mem(x, y, ws);
        fwd_stream_->submit(fwd_primitives_).wait();
    } 
    else
    {
        fwd_reset_mem(x, y,ws);
        fwd_stream_->rerun().wait();
    }
    return 0;
}

template<typename T>
int LocalResponseNormalization<T>::backward_setup(
	T* x,  int x_d1,  int x_d2,  int x_d3,  int x_d4,
	T* gy, int gy_d1, int gy_d2, int gy_d3, int gy_d4,
	T* gx, int gx_d1, int gx_d2, int gx_d3, int gx_d4)
{
	/* Backward lrn */
    memory::dims lrn_src_tz = {x_d1, x_d2, x_d3, x_d4};
    memory::dims lrn_diff_src_tz = {gx_d1, gx_d2, gx_d3, gx_d4};
    memory::dims lrn_diff_dst_tz = {gy_d1, gy_d2, gy_d3, gy_d4};

    lrn_bwd_user_src_mem_.reset(new memory({{{lrn_src_tz}, memory_data_type<T>(),
		p.data_format}, *eng}, x));
	lrn_diff_src_mem_.reset(new memory({{{lrn_diff_src_tz}, memory_data_type<T>(),
		p.data_format}, *eng}, gx));
	lrn_diff_dst_mem_.reset(new memory({{{lrn_diff_dst_tz}, memory_data_type<T>(),
		p.data_format}, *eng}, gy));

	lrn_bwd_src_desc.reset(new memory::desc({lrn_src_tz},
	    memory_data_type<T>(), p.diff_data_format));
	lrn_diff_src_desc.reset(new memory::desc({lrn_diff_src_tz},
	    memory_data_type<T>(), p.diff_data_format));
	lrn_diff_dst_desc.reset(new memory::desc({lrn_diff_dst_tz},
	    memory_data_type<T>(), p.diff_data_format));

    auto lrn_src_mem_ = lrn_bwd_user_src_mem_;


	lrn_bwd_desc_.reset(new lrn_backward::desc(p.aalgorithm,
		*lrn_bwd_src_desc, *lrn_diff_dst_desc, 
		p.local_size, p.alpha, p.beta,p.k));
	lrn_bwd_pd_.reset(new lrn_backward::primitive_desc(*lrn_bwd_desc_, *eng,
		*lrn_fwd_pd_));

    gx_mem_ = lrn_diff_src_mem_;
    gy_mem_ = lrn_diff_dst_mem_;
    bool reorder_x_p = false;
    bool reorder_y_p = false;

    if (memory::primitive_desc(lrn_bwd_pd_.get()->diff_dst_primitive_desc())
        != lrn_diff_dst_mem_->get_primitive_desc()) {
        gy_mem_.reset(new memory(lrn_bwd_pd_.get()->diff_dst_primitive_desc()));
        reorder_gy_ = reorder(*lrn_diff_dst_mem_, *gy_mem_);
        reorder_y_p = true;
    }

    if (memory::primitive_desc(lrn_bwd_pd_.get()->diff_src_primitive_desc())
        != lrn_diff_src_mem_->get_primitive_desc()) {
        gx_mem_.reset(new memory(
                            lrn_bwd_pd_.get()->diff_src_primitive_desc()));
        reorder_gx_ = reorder(*gx_mem_, *lrn_diff_src_mem_);
        reorder_x_p = true;
    }

    LOG(INFO) << "    reorder_dst_diff: " << reorder_y_p;
    LOG(INFO) << "    reorder_src_diff: " << reorder_x_p;

 
	lrn_bwd_.reset(new lrn_backward(*lrn_bwd_pd_, 
		*lrn_src_mem_, *gy_mem_, *workspace_memory_,*gx_mem_));

    if (reorder_y_p) bwd_primitives_.push_back(reorder_gy_);
	bwd_primitives_.push_back(*lrn_bwd_);
    if (reorder_x_p) bwd_primitives_.push_back(reorder_gx_);
    bwd_stream_.reset(new stream(stream::kind::eager));

    return 0;
}

template<typename T>
void LocalResponseNormalization<T>::bwd_reset_mem(T* x,T* gy,T* gx,T* ws)
{
    // lrn_fwd_user_src_mem_->set_data_handle(x);
    lrn_bwd_user_src_mem_->set_data_handle(x);
    lrn_diff_src_mem_->set_data_handle(gx);
    lrn_diff_dst_mem_->set_data_handle(gy);
    workspace_memory_ -> set_data_handle(ws);
    
}

template<typename T>
int LocalResponseNormalization<T>::backward(
	T* x,  int x_d1,  int x_d2,  int x_d3,  int x_d4,
	T* gy, int gy_d1, int gy_d2, int gy_d3, int gy_d4,
	T* gx, int gx_d1, int gx_d2, int gx_d3, int gx_d4,
	T* ws, int ws_d1, int ws_d2, int ws_d3, int ws_d4)
{
    // LOG(INFO) << "backward: " << x << " : " << x_size << " : " << gy << " : " << gy_size << " : " << gx << " : " << gx_size;
    if (!bwd_stream_) 
    {
        backward_setup(	
        	x, x_d1, x_d2, x_d3, x_d4,
			gy, gy_d1, gy_d2, gy_d3, gy_d4,
			gx, gx_d1,gx_d2, gx_d3, gx_d4);
        bwd_reset_mem(x, gy, gx,ws);
        bwd_stream_->submit(bwd_primitives_).wait();
    } 
    else 
    {
        bwd_reset_mem(x, gy, gx,ws);
        bwd_stream_->rerun().wait();
    }
    return 0;
}

template<typename T>
int LocalResponseNormalization<T>::forward()
{

	return 0;
}
template<typename T>
LocalResponseNormalization<T>* LocalResponseNormalization<T>::get_forward_object(
	int x_d1, int x_d2, int x_d3, int x_d4,
    int n, double k, double alpha, double beta, mkldnn::algorithm alg_kind)
{
	auto lrn_forward = dynamic_cast<LocalResponseNormalization<T>*>(
	    LayerFactory<T>::getInstance().getLRNLayer(x_d1,x_d2,x_d3,x_d4,n,k,alpha,beta));
	if (lrn_forward == NULL) 
	{
		lrn_forward = new LocalResponseNormalization<T>(n,k,alpha,beta,alg_kind);
		// LOG(INFO) << "new lrn obj " << lrn << " dim " << x_d1;
		LayerFactory<T>::getInstance().setLRNLayer(x_d1,x_d2,x_d3,x_d4,n,k,alpha,beta,lrn_forward);
	}
	return lrn_forward;
}
template<typename T>
LocalResponseNormalization<T>* LocalResponseNormalization<T>::get_backward_object(
    int x_d1, int x_d2, int x_d3, int x_d4,
    int n, double k, double alpha, double beta, mkldnn::algorithm alg_kind)
{
    auto lrn_backward = dynamic_cast<LocalResponseNormalization<T>*>(
        LayerFactory<T>::getInstance().getLRNLayer(x_d1,x_d2,x_d3,x_d4,n,k,alpha,beta));
    assert (lrn_backward != NULL);  // we must have already done forward before
    return lrn_backward;
}


template class LocalResponseNormalization<float>;
// template class LocalResponseNormalization<double>;


