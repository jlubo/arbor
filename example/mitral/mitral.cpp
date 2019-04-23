/*
 * A miniapp that demonstrates how to make a model with gap junctions
 *
 */

#include <fstream>
#include <iomanip>
#include <iostream>

#include <nlohmann/json.hpp>

#include <arbor/assert_macro.hpp>
#include <arbor/common_types.hpp>
#include <arbor/context.hpp>
#include <arbor/load_balance.hpp>
#include <arbor/cable_cell.hpp>
#include <arbor/profile/meter_manager.hpp>
#include <arbor/profile/profiler.hpp>
#include <arbor/simple_sampler.hpp>
#include <arbor/simulation.hpp>
#include <arbor/recipe.hpp>
#include <arbor/version.hpp>

#include <sup/ioutil.hpp>
#include <sup/json_meter.hpp>

#include "parameters.hpp"

#ifdef ARB_MPI_ENABLED
#include <mpi.h>
#include <sup/with_mpi.hpp>
#endif

using arb::cell_gid_type;
using arb::cell_lid_type;
using arb::cell_size_type;
using arb::cell_member_type;
using arb::cell_kind;
using arb::time_type;
using arb::cell_probe_address;

// Writes voltage trace as a json file.
void write_trace_json(const std::vector<arb::trace_data<double>>& trace);

// Generate a cell.
arb::cable_cell branch_cell(unsigned num_gj, double delay, double duration, bool tweak);

class gj_recipe: public arb::recipe {
public:
    gj_recipe(gap_params params):
        num_cells_(params.num_cells), num_gj_(params.num_gj), duration_(params.duration), gj_(params.gj), tweak_(params.tweak)
    {}

    cell_size_type num_cells() const override {
        return num_cells_;
    }

    arb::util::unique_any get_cell_description(cell_gid_type gid) const override {
        if (gid == 0) {
            return branch_cell(num_gj_, 0.0, duration_, false);
        } else {
            return branch_cell(num_gj_, 10.0, duration_, tweak_);
        }
    }

    cell_kind get_cell_kind(cell_gid_type gid) const override {
        return cell_kind::cable;
    }

    // Each cell has one spike detector (at the soma).
    cell_size_type num_sources(cell_gid_type gid) const override {
        return 0;
    }

    // The cell has one target synapse, which will be connected to cell gid-1.
    cell_size_type num_targets(cell_gid_type gid) const override {
        return 0;
    }

    // There is one probe (for measuring voltage at the soma) on the cell.
    cell_size_type num_probes(cell_gid_type gid)  const override {
        return 1;
    }

    arb::probe_info get_probe(cell_member_type id) const override {
        // Get the appropriate kind for measuring voltage.
        cell_probe_address::probe_kind kind = cell_probe_address::membrane_voltage;
        // Measure at the soma.
        arb::segment_location loc(0, 0.5);

        return arb::probe_info{id, kind, cell_probe_address{loc, kind}};
    }
    arb::util::any get_global_properties(cell_kind k) const override {
        arb::cable_cell_global_properties a;
        a.temperature_K = 308.15;
        return a;
    }

    std::vector<arb::gap_junction_connection> gap_junctions_on(cell_gid_type gid) const override{
        std::vector<arb::gap_junction_connection> conns;
        for (unsigned i = 0 ; i < num_gj_; i++) {
            conns.push_back(arb::gap_junction_connection({gid, i}, {(gid + 1) % num_cells_, i}, gj_*0.00037));
        }
        return conns;
    }

private:
    cell_size_type num_cells_;
    cell_size_type num_gj_;
    double duration_;
    bool gj_;
    bool tweak_;
};

struct cell_stats {
    using size_type = unsigned;
    size_type ncells = 0;
    size_type nsegs = 0;
    size_type ncomp = 0;

    cell_stats(arb::recipe& r) {
#ifdef ARB_MPI_ENABLED
        int nranks, rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        MPI_Comm_size(MPI_COMM_WORLD, &nranks);
        ncells = r.num_cells();
        size_type cells_per_rank = ncells/nranks;
        size_type b = rank*cells_per_rank;
        size_type e = (rank==nranks-1)? ncells: (rank+1)*cells_per_rank;
        size_type nsegs_tmp = 0;
        size_type ncomp_tmp = 0;
        for (size_type i=b; i<e; ++i) {
            auto c = arb::util::any_cast<arb::cable_cell>(r.get_cell_description(i));
            nsegs_tmp += c.num_segments();
            ncomp_tmp += c.num_compartments();
        }
        MPI_Allreduce(&nsegs_tmp, &nsegs, 1, MPI_UNSIGNED, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&ncomp_tmp, &ncomp, 1, MPI_UNSIGNED, MPI_SUM, MPI_COMM_WORLD);
#else
        ncells = r.num_cells();
        for (size_type i=0; i<ncells; ++i) {
            auto c = arb::util::any_cast<arb::cable_cell>(r.get_cell_description(i));
            nsegs += c.num_segments();
            ncomp += c.num_compartments();
        }
#endif
    }

    friend std::ostream& operator<<(std::ostream& o, const cell_stats& s) {
        return o << "cell stats: "
                 << s.ncells << " cells; "
                 << s.nsegs << " segments; "
                 << s.ncomp << " compartments.";
    }
};


int main(int argc, char** argv) {
    try {
        bool root = true;

#ifdef ARB_MPI_ENABLED
        sup::with_mpi guard(argc, argv, false);
        auto context = arb::make_context(arb::proc_allocation(), MPI_COMM_WORLD);
        {
            int rank;
            MPI_Comm_rank(MPI_COMM_WORLD, &rank);
            root = rank==0;
        }
#else
        auto context = arb::make_context();
#endif

#ifdef ARB_PROFILE_ENABLED
        arb::profile::profiler_initialize(context);
#endif

        std::cout << sup::mask_stream(root);

        // Print a banner with information about hardware configuration
        std::cout << "gpu:      " << (has_gpu(context)? "yes": "no") << "\n";
        std::cout << "threads:  " << num_threads(context) << "\n";
        std::cout << "mpi:      " << (has_mpi(context)? "yes": "no") << "\n";
        std::cout << "ranks:    " << num_ranks(context) << "\n" << std::endl;

        auto params = read_options(argc, argv);

        arb::profile::meter_manager meters;
        meters.start(context);

        // Create an instance of our recipe.
        gj_recipe recipe(params);

        auto decomp = arb::partition_load_balance(recipe, context);

        // Construct the model.
        arb::simulation sim(recipe, decomp, context);

        // Set up the probe that will measure voltage in the cell.

        auto sched = arb::regular_schedule(0.025);
        // This is where the voltage samples will be stored as (time, value) pairs
        std::vector<arb::trace_data<double>> voltage(recipe.num_cells());

        // Now attach the sampler at probe_id, with sampling schedule sched, writing to voltage
        for(unsigned i = 0; i < recipe.num_cells(); i++) {
            auto t = recipe.get_probe({i, 0});
            sim.add_sampler(arb::one_probe(t.id), sched, arb::make_simple_sampler(voltage[i]));
        }

        // Set up recording of spikes to a vector on the root process.
        std::vector<arb::spike> recorded_spikes;
        if (root) {
            sim.set_global_spike_callback(
                [&recorded_spikes](const std::vector<arb::spike>& spikes) {
                    recorded_spikes.insert(recorded_spikes.end(), spikes.begin(), spikes.end());
                });
        }

        meters.checkpoint("model-init", context);

        std::cout << "running simulation" << std::endl;
        // Run the simulation for 100 ms, with time steps of 0.025 ms.
        sim.run(params.duration, 0.025);

        meters.checkpoint("model-run", context);

        auto ns = sim.num_spikes();

        // Write spikes to file
        if (root) {
            std::cout << "\n" << ns << " spikes generated at rate of "
                      << params.duration/ns << " ms between spikes\n";
            std::ofstream fid("spikes.gdf");
            if (!fid.good()) {
                std::cerr << "Warning: unable to open file spikes.gdf for spike output\n";
            }
            else {
                char linebuf[45];
                for (auto spike: recorded_spikes) {
                    auto n = std::snprintf(
                        linebuf, sizeof(linebuf), "%u %.4f\n",
                        unsigned{spike.source.gid}, float(spike.time));
                    fid.write(linebuf, n);
                }
            }
        }

        // Write the samples to a json file.
        if (root) {
            write_trace_json(voltage);
        }

        auto report = arb::profile::make_meter_report(meters, context);
        std::cout << report;
    }
    catch (std::exception& e) {
        std::cerr << "exception caught in gap junction miniapp:\n" << e.what() << "\n";
        return 1;
    }

    return 0;
}

void write_trace_json(const std::vector<arb::trace_data<double>>& trace) {
    for (unsigned i = 0; i < trace.size(); i++) {
        std::string path = "./voltages_imp" + std::to_string(i) + ".json";

        nlohmann::json json;
        json["name"] = "arbor";
        json["units"] = "mV";
        json["cell"] = std::to_string(i);
        json["probe"] = "0";

        auto &jt = json["data"]["time"];
        auto &jy = json["data"]["voltage"];

        for (const auto &sample: trace[i]) {
            jt.push_back(sample.t);
            jy.push_back(sample.v);
        }

        std::ofstream file(path);
        file << std::setw(1) << json << "\n";
    }
}

arb::cable_cell branch_cell(unsigned num_gj, double delay, double duration, bool tweak) {
    arb::cable_cell cell;

    auto set_reg_params = [](auto seg, bool change_nax=false) {
        arb::mechanism_desc nax("nax");
        arb::mechanism_desc kdrmt("kdrmt");
        arb::mechanism_desc kamt("kamt");
        arb::mechanism_desc pas("pas");

        nax["gbar"] = change_nax ? 0.0 : 0.04;
        nax["sh"] = 10;
        kdrmt["gbar"] = 0.0001;
        kamt["gbar"] = 0.004;
        pas["g"] =  1.0/12000.0;
        pas["e"] = change_nax ? -61.759276 : -61.772335;

        seg->rL = 150;
        seg->cm = 0.018;
        seg->add_mechanism(nax);
        seg->add_mechanism(kdrmt);
        seg->add_mechanism(kamt);
        seg->add_mechanism(pas);
    };

    auto set_axon_params = [&](auto seg) {
        arb::mechanism_desc nax("nax");
        arb::mechanism_desc kdrmt("kdrmt");
        arb::mechanism_desc kamt("kamt");
        arb::mechanism_desc pas("pas");

        nax["gbar"] = 0.4;
        nax["sh"] = 0;
        kdrmt["gbar"] = 0.0001;
        kamt["gbar"] = 0.04;
        pas["g"] = 1.0/1000.0;
        pas["e"] =  -62.953635;

        seg->rL = 150;
        seg->cm = 0.018;
        seg->add_mechanism(nax);
        seg->add_mechanism(kdrmt);
        seg->add_mechanism(kamt);
        seg->add_mechanism(pas);
    };

    auto soma = cell.add_soma(22.360679775/2.0);
    set_reg_params(soma);

    /*arb::i_clamp stim(delay, duration, 0.2);
    cell.add_stimulus({0, 0.25}, stim);*/

    auto dend = cell.add_cable(0, arb::section_kind::dendrite, 3.0/2.0, 3.0/2.0, 300); //cable 1
    dend->set_compartments(5);
    set_reg_params(dend);

    auto dend_min0 = cell.add_cable(0, arb::section_kind::dendrite, 2.0/2.0, 2.0/2.0, 100); //cable 2
    dend_min0->set_compartments(4);
    set_reg_params(dend_min0);

    auto dend_min1 = cell.add_cable(0, arb::section_kind::dendrite, 2.0/2.0, 2.0/2.0, 100); //cable 3
    dend_min1->set_compartments(4);
    set_reg_params(dend_min1);

    auto hillock = cell.add_cable(0, arb::section_kind::dendrite, 20/2.0, 20.0/2.0, 5); //cable 4
    hillock->set_compartments(300);
    set_reg_params(hillock);

    auto init_seg = cell.add_cable(4, arb::section_kind::axon, 1.5/2.0, 1.5/2.0, 30); //cable 5
    init_seg->set_compartments(300);
    set_axon_params(init_seg);

    for(unsigned i = 0; i < num_gj; i++) {
        auto tuft = cell.add_cable(1, arb::section_kind::dendrite, 0.4/2.0, 0.4/2.0, 300); //cable 6
        tuft->set_compartments(30);
        set_reg_params(tuft, tweak);

        arb::i_clamp stim(delay, duration, 0.02);
        cell.add_stimulus({6 + i, 0.25}, stim);

        cell.add_gap_junction({6 + i, 0.95});
    }

    return cell;
}
