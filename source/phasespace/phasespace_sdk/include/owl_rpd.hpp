// owl_rpd.hpp
// OWL C API v2.0

/***
Copyright (c) PhaseSpace, Inc 2017

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL PHASESPACE, INC
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***/

#ifndef OWL_RPD_HPP
#define OWL_RPD_HPP

#ifdef WIN32
#ifdef __DLL
#define OWLAPI __declspec(dllexport)
#else // !__DLL
#define OWLAPI __declspec(dllimport)
#endif // __DLL
#else // ! WIN32
#define OWLAPI
#endif // WIN32

namespace OWL {

  //// RPD ////

  struct OWLAPI RPD {

    enum { SAVE = 1, LOAD = 2 };

    int fd;
    int sock;
    int mode;

    int _write, _read, _send, _recv;

    std::vector<char> buffer;

    RPD();
    ~RPD();

    int open(const char *servername, const char *filename, int mode);
    bool close();
    bool flush();
    bool done();
    int send(long timeout=0);
    int recv(long timeout=0);
  };

} // namespace OWL

////

#endif // OWL_RPD_HPP
