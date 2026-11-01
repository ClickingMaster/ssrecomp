#include "save_backups.h"
#include "cheats.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main()
{
    namespace fs=std::filesystem;
    const fs::path root=fs::temp_directory_path()/"fulldex-player-tools-tests";
    assert(!fs::exists(root));fs::create_directories(root/"backups");
    auto put=[](const fs::path& p,const std::string& data){std::ofstream(p,std::ios::binary)<<data;};
    const std::string save=(root/"heartgold.sav").string();
    put(save,"current!");
    const std::string backup=(root/"backups/heartgold-20261101-120000.sav").string();
    put(backup,"earlier!");
    put(root/"backups/black-20261101-120000.sav","foreign!");
    put(root/"backups/heartgold-invalid.sav","invalid!");
    auto list=SaveBackups::List(save);assert(list.size()==1 && list[0].label=="2026-11-01 12:00:00");
    std::vector<unsigned char> data;std::string error;
    assert(!SaveBackups::Restore(save,(root/"backups/black-20261101-120000.sav").string(),8,data,error));
    assert(SaveBackups::Read(save)==std::vector<unsigned char>({'c','u','r','r','e','n','t','!'}));
    assert(!SaveBackups::Restore(save,backup,4,data,error));
    assert(SaveBackups::Restore(save,backup,8,data,error));
    assert(SaveBackups::Read(save)==data && SaveBackups::Read(save+".before-restore.sav")==std::vector<unsigned char>({'c','u','r','r','e','n','t','!'}));
    put(save+".restore-tmp","interrupted");
    assert(!SaveBackups::Restore(save,backup,8,data,error));
    assert(SaveBackups::Read(save)==std::vector<unsigned char>({'e','a','r','l','i','e','r','!'}));
    Cheats::Cheat cheat;cheat.name="RARE CANDY";cheat.category="ITEMS";cheat.note="HOLD L+R";
    assert(Cheats::Matches(cheat,"candy items") && Cheats::Matches(cheat,"l+r") && Cheats::Matches(cheat,""));
    assert(!Cheats::Matches(cheat,"rare pokemon"));
    const std::string longText="A VERY LONG DESCRIPTION WITH MANY WORDS AND "+std::string(100,'X');
    const auto lines=Cheats::Lines(longText,12);std::string recovered;
    for(const auto& line:lines){assert(line.size()<=12);recovered+=line;}
    std::string expected;for(char c:longText)if(c!=' ')expected+=c;
    std::string actual;for(char c:recovered)if(c!=' ')actual+=c;assert(actual==expected);
    Cheats::List codes;codes.folder=(root/"cheats").string();codes.code="TEST";
    codes.Add("FIRST","first",{0,0},7);codes.Add("SECOND","second",{0,0},7);
    codes.Toggle(0);codes.Toggle(1);assert(!codes.items[0].on && codes.items[1].on);
    fs::remove_all(root); // Only this test's synthetic directory.
    std::cout<<"PASS: save restore, safety copy, invalid backup refusal, search, full details and exclusive cheats\n";
}
