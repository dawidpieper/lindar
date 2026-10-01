lnd_add_module(graph CONFIG config.def
    SOURCES control.c mixer.c splitter.c source_object.c transport.c gain.c topology.c walk.c native.c input.c handles.c context.c node.c render.c source_node.c processor.c sound.c ring.c buffer_source.c proc_source.c
    REQUIRES buffers pcm_float audio
    HEADER lindar_graph.h
    FREE lnd_graph_free UPDATE lnd_graph_update PRIORITY 30)
