
Core features

- [x] support midi inputs
- [x] support audio inputs
- [x] support mixer vol/pan
- [x] support more than 4 tracks
- [x] support automation clip naming
- [ ] support clip alias
- [ ] support clip browser
- [ ] support track group
- [ ] support aux track
- [ ] support region loop and bounce

C++ backend

- [ ] setup C++ linter in local and CI
- [x] setup C++ formatter in local and CI
- [x] move C++ files to hibiki/engine
- [x] move proto files to hibiki/pb
- [x] create abstract class for audio outputs


Java/Clojure frontend

- [ ] setup Java linter in local and CI
- [x] setup Java formatter in local and CI
- [ ] setup Clojure linter in local and CI
- [ ] setup Clojure formatter in local and CI


For fade in/out, I want to have top-left (fade-in) and top-right (fade-out) corner triangle markers which can be horizontally seeked to apply the length of linear fading

I also want you to bounce-in-place feature. It renders the midi or audio clip with inst and effects as bounced audio replacing the original clip (muted). I need two features. muting the 