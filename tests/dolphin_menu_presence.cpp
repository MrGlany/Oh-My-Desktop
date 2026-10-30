#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QAction>
#include <KFileItemActions>
#include <KFileItemListProperties>
int main(int argc,char **argv){
    QApplication app(argc,argv);
    if(argc!=2)return 10;
    const bool expected=QString::fromLocal8Bit(argv[1])=="present";
    if(!expected&&QString::fromLocal8Bit(argv[1])!="absent")return 11;
    QTemporaryDir temp;if(!QDir().mkdir(temp.path()+"/Folder"))return 12;
    KFileItemList items;items<<KFileItem(QUrl::fromLocalFile(temp.path()+"/Folder"));
    KFileItemActions builder;builder.setItemListProperties(KFileItemListProperties(items));
    bool found=false;for(auto *action:builder.createServiceMenuActions())if(action->text()==QString::fromUtf8("Создать ярлык на рабочем столе"))found=true;
    if(found!=expected)return 13;
    puts(found?"KDE menu present":"KDE menu absent");return 0;
}
