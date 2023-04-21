# dlb_lip_tool.exe

The Dolby LIP tool is an example code of how system can interact with the dlb_lip library. The Dolby LIP tool integrates the libCEC library as a CEC driver.

Command line parameters:
    Manddatory:
        -x: [file] Reads LIP parameters of the device from XML file.
    Optional:
        -a: Act as ARC receiver - answer to CEC ARC communication
        -c: [file] Read real-time commands from file
        -f: [file] Write all LIP and libCEC log message with timestamps to a file
        -n: No cache - disable caching
        -p: [port] Pulse8 cec adapter port name(eg. COM5)
        -s: [file] Write current LIP tool state to a file
        -v: verbosity flag
        
Supported real-time commands:
    tx - send custom CEC message
        Example:
            tx 40:a0:00:d0:46:10
    q - stop processing and exit LIP Tool
    wait downstream - wait for downstream device
    wait upstream - wait for upstream device(max wait time is 32s)
    wait <milliseconds> - wait for <milliseconds> ms before executing the next command
    req audio_latency <audio_format> <audio_subtype> <audio_ext> - send a downstream request for an audio latency of a <audio_format> <audio_subtype> <audio_ext>
        Possible values:
            <audio_format>: MAT, DDP, DD
            <audio_subtype>: 0..3
            <audio_ext>: 0..31
         Example:
            req audio_latency MAT 0 0
    req video_latency <vic> <color_format> <hdr_mode> - send a downstream request for a video latency of a <vic> <color_format> <hdr_mode>.
        Possible values: 
            <vic>: VIC1...VIC219
            <color_format>: HDR_STATIC, HDR_DYNAMIC, DV
            <hdr_mode>: depend on color_format <HDR_STATIC: SDR, HDR, SMPTE, HLG; HDR_DYNAMIC: SMPTE_ST_2094_10, ETSI, ITU, SMPTE_ST_2094_40; DV: SINK, SOURCE>
        Example:
            req video_latency VIC96 HDR_STATIC HDR
    req av_latency <audio_format> <audio_subtype> <audio_ext> <vic> <color_format> <hdr_mode> - send a downstream request for audio and video latency for a given combination of a/v format. 
        Example:
            req av_latency DDP 0 0 VIC96 HDR_STATIC SDR
    update audio_latency <audio_format> <audio_subtype> <audio_ext> <latency> - update audio latency and send UPDATE_UUID to upstream device
        Possible values: 
            <latency>: <0...255>
        Example:
            update audio_latency DDP 0 0 77
    update video_latency <vic> <color_format> <hdr_mode> <latency> - update video latency and send UPDATE_UUID to upstream device
        Possible values: 
            <latency>: <0...255>
        Example:
            update video_latency VIC96 HDR_STATIC SDR 88
    update av_latency <audio_format> <audio_subtype> <audio_ext> <vic> <color_format> <hdr_mode> <audio_latency> <video_latency> 
        Example:
            update av_latency DDP 0 0 VIC96 HDR_STATIC SDR 112 210
    update uuid <new_uuid> - send updated uuid to upstream device
        Example:
            update uuid 123456
    on update uuid <audio_format> <audio_subtype> <audio_ext> <vic> <color_format> <hdr_mode> - register audio/video format, request_av_latency with those formats will be send to downstream device after receiving UPDATE_UUID opcode
        Example:
            on update uuid DDP 0 0 VIC96 HDR_STATIC SDR
    random <count> - send <count> random LIP messages to connected LIP devices.
        Example:
            random 10

Cache:
    Please note that dlb_lip library implements caching. Multiple request for the same audio or video format will be served from cache.
    Example:
        req av_latency DDP 0 0 VIC96 HDR_STATIC SDR
        - REQUEST_AV_LATENCY will sent to downstream device
        req av_latency DDP 0 0 VIC98 HDR_STATIC SDR
        - audio latency for "DDP 0 0" is cached at that point, only request for video latency will be sent to downstream device

XML:
    XML configuration files are located in xml_configs directory.
    Example:

    <LIP_Config>
    <DeviceParams>
         <!--Device UUID-->
        <UUID>0xAABBCCDD00</UUID>
        <!--Physical address of the device.-->
        <PhysicalAddress>1.0.0.0</PhysicalAddress>
        <!--Logical address of connected downstream device.-->
        <LogicalAddressMap>5</LogicalAddressMap>
        <!--Device type(possible values: audio, playback, tv).-->
        <DeviceType>playback</DeviceType>
        <!--Information about whether a simulated device act as audio or video renderer(possible values: audio, video, av).-->
        <Renderer>video</Renderer>
    </DeviceParams>
    
    <VideoLatencies>
        <!--Contains sample audio latency values for various combination of video parameters.-->
        <VidLatency VIC="96">100</VidLatency>
        <VidLatency VIC="96" color_format="DV">130</VidLatency>
        <VidLatency VIC="96" color_format="HDR_STATIC">120</VidLatency>
        <VidLatency VIC="96" color_format="HDR_DYNAMIC">110</VidLatency>
        <VidLatency VIC="97">150</VidLatency>
    </VideoLatencies>
    
    <AudioLatencies>
        <!--Contains sample audio latency values for combination of audio formats and audio properties.-->
		<AudLatency format="PCM">50</AudLatency>
        <AudLatency format="MAT">50</AudLatency>
        <AudLatency format="MAT" subtype="0" ext="0">51</AudLatency>
        <AudLatency format="MAT" subtype="1" ext="31">52</AudLatency>
        <AudLatency format="DDP">100</AudLatency>
        <AudLatency format="DD" subtype="0">110</AudLatency>
        <AudLatency format="DD" subtype="3">119</AudLatency>
    </AudioLatencies>  
    </LIP_Config>

