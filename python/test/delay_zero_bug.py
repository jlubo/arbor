import arbor


class Recipe(arbor.recipe):
    """Implementation of Arbor simulation recipe."""

    def __init__(self, dt, delay):
        """Initialize the recipe object."""

        arbor.recipe.__init__(self)

        self.the_props = arbor.neuron_cable_properties()
        self.the_cat = arbor.default_catalogue()
        self.the_props.catalogue = self.the_cat
        self.dt = dt
        self.delay = delay

    def num_cells(self):
        """Return the number of cells."""
        return 2

    def num_sources(self, gid):
        """Return the number of spikes sources on gid."""
        if gid == 0:
            return 0
        else:
            return 1
        
    def num_targets(self, gid):
        """Return the number of post-synaptic targets on gid."""
        if gid == 0:
            return 1
        elif gid == 1:
            return 0

    def cell_kind(self, gid):
        """Return type of cell with gid."""
        return arbor.cell_kind.cable

    def cell_description(self, gid):
        """Return cell description of gid."""

        # morphology
        tree = arbor.segment_tree()
        radius = 1

        tree.append(arbor.mnpos,
                    arbor.mpoint(-radius, 0, 0, radius),
                    arbor.mpoint(radius, 0, 0, radius),
                    tag=1)

        labels = arbor.label_dict({'center': '(location 0 0.5)'})

        # cell mechanism
        decor = arbor.decor()
        neuron = arbor.mechanism("hh")
        if gid == 1:
            # add incoming synapse
            mech_expsyn = arbor.mechanism('expsyn_stdp')
            decor.place('"center"', arbor.synapse(mech_expsyn), "expsyn_stdp_post")
        decor.place('"center"', arbor.threshold_detector(1), "spike_detector")
        decor.paint('(all)', arbor.density(neuron))

        return arbor.cable_cell(tree, decor, labels)

    def connections_on(self, gid):
        """Defines the list of synaptic connections incoming to the neuron given by gid."""
        
        policy = arbor.selection_policy.univalent
        weight = 0
        delay = self.delay

        # neuron with gid 0 is presynaptic
        if gid == 0:
            conn = [ ]
        
        # neuron with gid 1 is postsynaptic
        elif gid == 1:
            src = 0
            conn = [arbor.connection((src, "spike_detector"), ('expsyn_stdp_post', policy), weight, delay)]

        return conn

    def event_generators(self, gid):
        """Event generator for input to synapses."""
        if gid == 1:
            return [arbor.event_generator("expsyn_stdp_post",
                                        0.,
                                        arbor.explicit_schedule([1,2,3,4,5,6]))]
        
        return []

    def probes(self, gid):
        """Return probes on gid."""

        probe_list = [arbor.cable_probe_membrane_voltage('"center"')]

        # neuron with gid 1 is postsynaptic
        if gid == 1:
            probe_list.append(arbor.cable_probe_point_state(0, "expsyn_stdp", "weight_plastic"))
        
        return probe_list
    
    def global_properties(self, kind):
        """Return the global properties."""
        assert kind == arbor.cell_kind.cable

        return self.the_props

if __name__ == "__main__":

    dt = 0.01
    t_max = 10
    #delay = dt # may not be <= 0
    delay = 0
    recipe = Recipe(dt, delay)

    context = arbor.context()
    domains = arbor.partition_load_balance(recipe, context)
    sim = arbor.simulation(recipe, context, domains)

    sim.record(arbor.spike_recording.all)

    reg_sched = arbor.regular_schedule(dt)

    handle_v_0 = sim.sample((0, 0), reg_sched)
    handle_v_1 = sim.sample((1, 0), reg_sched)
    handle_weight_plastic = sim.sample((1, 1), reg_sched)

    sim.run(tfinal=t_max,
            dt=dt)

    # read out variables
    if len(sim.samples(handle_v_0)) > 0:
        data_buf, _ = sim.samples(handle_v_0)[0]
        assert len(data_buf) > 0
    if len(sim.samples(handle_v_1)) > 0:
        data_buf, _ = sim.samples(handle_v_1)[0]
        assert len(data_buf) > 0
    if len(sim.samples(handle_weight_plastic)) > 0:
        data_buf, _ = sim.samples(handle_weight_plastic)[0]
        assert len(data_buf) > 0
