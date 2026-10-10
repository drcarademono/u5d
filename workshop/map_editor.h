#pragma once
#include "project.h"
#include <QtWidgets>
#include <functional>

// View-only state; never serialized into game resources or mod packages.
struct MapViewState {
    double zoom = 2;
    bool fit = false, grid = false, comparison = false, outline = false;
    QRect selection;
    QPoint keyboardCell = {0, 0};
    int brush = 1, tool = 0, schedule = 0, npc = -1;
    bool ghosts = false;
    QPointF center = {-1, -1}; // Map cells, independent of display scale.
};

class MapDocument {
  public:
    MapDocument(Project *project, const QString &resource);
    QVector<U5::MapPage> pages;
    QByteArray terrain(int page, bool original = false) const;
    QMap<QString, QByteArray> changes(int page, const QByteArray &terrain) const;
    static bool supported(const QString &resource);
    static QString tileName(int id);
    static QString tileCategory(int id);
    QImage terrainImage(int page, const QVector<QImage> &tiles, bool ids) const;

  private:
    Project *project;
    QString resource;
};

struct NpcLocation {
    int x, y, floor, ai;
};
class MapNpcDocument {
  public:
    MapNpcDocument(Project *project, QString resource);
    static int locationIndex(int slot);
    NpcLocation location(int settlement, int npc, int slot) const;
    int hour(int settlement, int npc, int slot) const;
    int sprite(int settlement, int npc) const;
    int dialogue(int settlement, int npc) const;
    QByteArray move(int settlement, int npc, int slot, NpcLocation destination) const;

  private:
    Project *project;
    QString resource;
    int offset(int settlement, int npc) const;
};

struct MapBrushState {
    QList<int> favorites, recent;
};

class MapCanvas : public QWidget {
  public:
    explicit MapCanvas(QWidget *parent = nullptr);
    enum Tool { Pencil, Eyedropper, Pan, InspectNpc, Select, Rectangle, Fill };
    QByteArray ids, original;
    QRect selection;
    bool outline = false, comparison = false;
    QString operation = "Paint";
    std::function<void()> changed, selectionChanged;
    std::function<void(const QString &)> feedback;
    QString clipboardMime = "application/x-impera-terrain", clipboardMagic = "IMPTILE1";
    static QIcon toolIcon(int tool);
    bool copySelection();
    bool beginPaste();
    bool pasting() const { return !stamp.isEmpty(); }
    void clearSelection();
    int pasteChangedCells() const;
    QVector<QImage> tiles;
    int side = 32, brush = 1, tool = 0;
    double zoom = 2;
    bool grid = false;
    struct Actor {
        int x, y, tile, npc;
        QString label;
    };
    QVector<Actor> actors, ghosts;
    int selectedNpc = -1;
    QPoint keyboardCell = {0, 0};
    std::function<int(const QList<int> &)> chooseActor;
    std::function<bool(int, QPoint)> moveNpc;
    std::function<bool(const QByteArray &)> commit;
    std::function<void(int, int, int)> inspect;
    std::function<void(int)> pick, chooseNpc;
    std::function<void(int)> chooseTool;
    void resizeMap();
    QPoint cellAt(QPointF position) const;
    bool painting() const { return stroke || npcDrag; }
    void cancelStroke();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool event(QEvent *) override;

  private:
    bool stroke = false, npcDrag = false;
    QPoint npcOrigin, npcDestination;
    QByteArray before, stamp;
    QSize stampSize;
    QPoint start = {-1, -1};
    QRect previousSelection;
    void finishEdit();
    void previewRectangle(QPoint cell);
    void flood(QPoint cell);
    bool editable(QPoint cell) const;
    bool pasteFits() const;
    void pasteAt(QPoint cell);
    void say(const QString &message);
    QPoint last = {-1, -1}, hover = {-1, -1};
    void point(QPointF position, bool draw);
    void line(QPoint from, QPoint to);
};

class MapView : public QScrollArea {
  public:
    explicit MapView(MapCanvas *canvas);
    std::function<void()> changed;
    void setZoom(double scale, QPoint anchor = {-1, -1}, bool fitting = false);
    void fitMap();
    void centerMap(QPointF cell);
    QPointF mapCenter() const;
    bool fitting = false;

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    MapCanvas *canvas;
    bool space = false, panning = false;
    QPoint panAt;
};

class MapMinimap : public QWidget {
  public:
    MapMinimap(MapCanvas *canvas, MapView *view);
    void refresh();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

  private:
    MapCanvas *canvas;
    MapView *view;
    QImage cache;
    QRectF imageRect() const;
    void navigate(QPointF position);
};

class MapWorkspace : public QWidget {
  public:
    MapWorkspace(Project *project, const QString &resource, QMap<QString, MapViewState> *states,
                 QMap<QString, int> *navigation,
                 std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commit,
                 std::function<void(const QString &)> navigate, MapBrushState *brushes = nullptr,
                 QWidget *parent = nullptr);
    void cancelGesture();

  private:
    Project *project;
    QString resource, key;
    MapDocument document;
    QMap<QString, MapViewState> *states;
    QMap<QString, int> *navigation;
    MapCanvas *canvas;
    MapView *view;
    QComboBox *page, *zoom, *schedule;
    QButtonGroup *tool;
    QCheckBox *comparison;
    MapMinimap *minimap;
    QLabel *selectionLabel;
    QComboBox *paletteFilter;
    QPushButton *favorite;
    QLineEdit *tileSearch;
    MapBrushState localBrushes, *brushes;
    void filterPalette();
    QSet<int> overworldTiles;
    QStringList tileCategories(int id) const;
    void updateSelection();
    QListWidget *palette;
    QTreeWidget *maps;
    QLabel *brushImage, *brushLabel, *coordinate;
    bool restoring = true;
    void saveView();
    void loadPage();
    int currentPage() const;
    void populateFloors(int index);
    void selectPage(int index);
    void setBrush(int id);
    void updateZoom();
    void exportImage(bool ids, bool preview = false);
    QStackedWidget *inspector;
    QComboBox *npcList;
    QLabel *npcInfo, *npcSprite;
    QSpinBox *npcX, *npcY, *npcFloor, *npcStartHour;
    QCheckBox *npcGhosts;
    QPushButton *npcConversation;
    QString npcResource, talkResource;
    QMap<int, QString> npcNames;
    QMap<int, int> conversationIndices;
    std::function<bool(const QMap<QString, QByteArray> &, const QString &)> commitResources;
    std::function<void(const QString &)> navigateResource;
    void refreshNpcs();
    void refreshCombat();
    void createCombatInspector(QVBoxLayout *layout);
    QComboBox *combatEntry = nullptr, *combatEntity = nullptr;
    QCheckBox *combatPreview = nullptr;
    QLabel *combatInfo = nullptr;
    bool moveCombat(int entity, QPoint destination);
    void selectNpc(int npc);
    void locateNpc();
    bool moveNpcTo(int npc, int x, int y, int floor);
    QString npcName(int npc) const;
    void createNpcInspector(QVBoxLayout *layout);
};
