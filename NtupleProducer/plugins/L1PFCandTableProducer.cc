// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"

#include "FWCore/Framework/interface/Event.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Common/interface/View.h"

#include "DataFormats/Candidate/interface/Candidate.h"

#include "DataFormats/Math/interface/deltaR.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"

#include "CommonTools/Utils/interface/StringCutObjectSelector.h"
#include "CommonTools/Utils/interface/StringObjectFunction.h"

#include <algorithm>
#include <string>

class L1PFCandTableProducer : public edm::global::EDProducer<>  {
    public:
        explicit L1PFCandTableProducer(const edm::ParameterSet&);
        ~L1PFCandTableProducer();

    private:
        virtual void produce(edm::StreamID id, edm::Event& iEvent, const edm::EventSetup& iSetup) const override;

        StringCutObjectSelector<reco::Candidate> sel_;

        struct ExtraVar {
            std::string name, expr;
            bool isUint32;  // True if user specified "uint32_int", false for float (default)
            StringObjectFunction<reco::Candidate> func;
            
            ExtraVar(const std::string & n, const std::string & expr) 
                : name(n), expr(expr), isUint32(false), func(expr, true) {}
        };
        std::vector<ExtraVar> extraVars_;

        struct CandRecord {
            public:
                std::string coll;
                edm::EDGetTokenT<reco::CandidateView> src;
                StringCutObjectSelector<reco::Candidate> sel;
                
                CandRecord(const std::string & name, const edm::EDGetTokenT<reco::CandidateView> & tag, const edm::ParameterSet & pset) :
                    coll(name), src(tag), 
                    sel(pset.existsAs<std::string>(name+"_sel") ? pset.getParameter<std::string>(name+"_sel") : "", true) {}
        };
        std::vector<CandRecord> cands_;
};

L1PFCandTableProducer::L1PFCandTableProducer(const edm::ParameterSet& iConfig) :
    sel_(iConfig.getParameter<std::string>("commonSel"), true)
{
    edm::ParameterSet cands = iConfig.getParameter<edm::ParameterSet>("cands");
    auto candnames = cands.getParameterNamesForType<edm::InputTag>();
    
    for (const std::string & name : candnames) {
        cands_.emplace_back(name, consumes<reco::CandidateView>(cands.getParameter<edm::InputTag>(name)), cands);
        produces<nanoaod::FlatTable>(name+"Cands");
    }

    if (iConfig.existsAs<edm::ParameterSet>("moreVariables")) {
        edm::ParameterSet vars = iConfig.getParameter<edm::ParameterSet>("moreVariables");
        auto morenames = vars.getParameterNamesForType<std::string>();
        
        for (const std::string & varname : morenames) {
            std::string expr = vars.getParameter<std::string>(varname);
            
            // Check if user specified "uint32_int" type
            // Config format: "expression:uint32_int" or just "expression" (defaults to float)
            // E.g., "hwQual:uint32_int" -> stored as uint32_t
            //       "hwQual" -> stored as float (default)
            bool isUint32 = false;
            size_t colonPos = expr.find(':');
            
            if (colonPos != std::string::npos) {
                std::string typeStr = expr.substr(colonPos + 1);
                expr = expr.substr(0, colonPos);
                
                if (typeStr == "uint32_int") {
                    isUint32 = true;
                } else {
                    throw cms::Exception("Configuration") 
                        << "Invalid type specification for variable '" << varname << "': '" << typeStr 
                        << "'. Only 'uint32_int' is supported. Default is float.";
                }
            }
            
            extraVars_.emplace_back(varname, expr);
            extraVars_.back().isUint32 = isUint32;
        }
    }
 }

L1PFCandTableProducer::~L1PFCandTableProducer() { }

// ------------ method called for each event  ------------
void
L1PFCandTableProducer::produce(edm::StreamID id, edm::Event& iEvent, const edm::EventSetup& iSetup) const
{
    edm::Handle<reco::CandidateView> src;
    std::vector<const reco::Candidate *> selected;
    std::vector<float> vals_float;
    std::vector<uint32_t> vals_uint32;
    
    for (auto & cands : cands_) {
        // get and select
        iEvent.getByToken(cands.src, src);
        for (const auto & j : *src) {
            if (sel_(j) && cands.sel(j)) {
                selected.push_back(&j);
            }
        }
        
        // create the table
        unsigned int ncands = selected.size();
        auto out = std::make_unique<nanoaod::FlatTable>(ncands, cands.coll+"Cands", false);

        // fill basic info (always float)
        vals_float.resize(ncands); 
        for (unsigned int i = 0; i < ncands; ++i) {
            vals_float[i] = selected[i]->pt();
        }
        out->addColumn<float>("pt", vals_float, "pt of cand");
        
        for (unsigned int i = 0; i < ncands; ++i) {
            vals_float[i] = selected[i]->eta();
        }
        out->addColumn<float>("eta", vals_float, "eta of cand");
        
        for (unsigned int i = 0; i < ncands; ++i) {
            vals_float[i] = selected[i]->phi();
        }
        out->addColumn<float>("phi", vals_float, "phi of cand");
        
        for (unsigned int i = 0; i < ncands; ++i) {
            vals_float[i] = selected[i]->mass();
        }
        out->addColumn<float>("mass", vals_float, "mass of cand");

        // fill extra vars: float by default, uint32 if user specified "uint32_int"
        for (const auto & evar : extraVars_) {
            if (evar.isUint32) {
                // Store as uint32
                vals_uint32.resize(ncands);
                for (unsigned int i = 0; i < ncands; ++i) {
                    vals_uint32[i] = static_cast<uint32_t>(evar.func(*selected[i]));
                }
                out->addColumn<uint32_t>(evar.name, vals_uint32, evar.expr);
            } else {
                // Store as float (default)
                vals_float.resize(ncands);
                for (unsigned int i = 0; i < ncands; ++i) {
                    vals_float[i] = evar.func(*selected[i]);
                }
                out->addColumn<float>(evar.name, vals_float, evar.expr);
            }
        }
        
        // save to the event branches
        iEvent.put(std::move(out), cands.coll+"Cands");

        // clear
        selected.clear();
    }
}

//define this as a plug-in
#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(L1PFCandTableProducer);
