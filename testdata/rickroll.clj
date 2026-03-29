(hbk/add-timeline-clip! 0 "testdata/rickroll.mid" 0.0 4.0)
(hbk/load-plugin! 0 "testdata/Dexed.vst3")

(hbk/play!)                     ; start playback
(Thread/sleep 10000)
(hbk/stop!)
(hbk/save! "/tmp/rickroll.hbk")
(System/exit 0)
