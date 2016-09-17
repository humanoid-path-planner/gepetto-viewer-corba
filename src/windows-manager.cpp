// Copyright (c) 2015, Joseph Mirabel
// Authors: Joseph Mirabel (joseph.mirabel@laas.fr)
//
// This file is part of gepetto-viewer.
// gepetto-viewer is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// gepetto-viewer is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// gepetto-viewer. If not, see <http://www.gnu.org/licenses/>.

#include "gepetto/viewer/corba/windows-manager.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

#include <boost/thread.hpp>

#include <osgDB/WriteFile>

#include <gepetto/viewer/window-manager.h>
#include <gepetto/viewer/node.h>
#include <gepetto/viewer/group-node.h>
#include <gepetto/viewer/leaf-node-box.h>
#include <gepetto/viewer/leaf-node-capsule.h>
#include <gepetto/viewer/leaf-node-cone.h>
#include <gepetto/viewer/leaf-node-cylinder.h>
#include <gepetto/viewer/leaf-node-line.h>
#include <gepetto/viewer/leaf-node-face.h>
#include <gepetto/viewer/leaf-node-sphere.h>
#include <gepetto/viewer/leaf-node-arrow.h>
#include <gepetto/viewer/leaf-node-light.h>
#include <gepetto/viewer/leaf-node-xyzaxis.h>
#include <gepetto/viewer/node-rod.h>
#include <gepetto/viewer/roadmap-viewer.h>
#include <gepetto/viewer/macros.h>
#include <gepetto/viewer/config-osg.h>
#include <gepetto/viewer/leaf-node-ground.h>
#include <gepetto/viewer/leaf-node-collada.h>
#include <gepetto/viewer/urdf-parser.h>
#include <gepetto/viewer/blender-geom-writer.h>

#include "gepetto/viewer/corba/graphical-interface.hh"

#define RETURN_IF_NODE_EXISTS(name)                                            \
  if (nodeExists(name)) {                                                      \
    std::cout << "\"" << name << "\" already exist." << std::endl;             \
    return false;                                                              \
  }

namespace graphics {
    namespace {
      typedef std::map<std::string, NodePtr_t>::iterator          NodeMapIt;
      typedef std::map<std::string, NodePtr_t>::const_iterator    NodeMapConstIt;

      struct ApplyConfigurationFunctor {
        void operator() (const NodeConfiguration& nc) const
        {
            nc.node->applyConfiguration ( nc.position, nc.quat);
        }
      };
    }

    WindowsManager::WindowsManager () :
        windowManagers_ (), nodes_ (), groupNodes_ (),roadmapNodes_(),
        mtx_ (), rate_ (20), newNodeConfigurations_ (),
        autoCaptureTransform_ (false)
    {}

    WindowsManager::WindowID WindowsManager::addWindow (std::string winName,
            WindowManagerPtr_t newWindow)
    {
      WindowID windowId = (WindowID) windowManagers_.size ();
        windowIDmap_ [winName] = windowId;
        windowManagers_.push_back (newWindow);
        return windowId;
    }

    WindowsManagerPtr_t WindowsManager::create ()
    {
      WindowsManagerPtr_t shPtr (new WindowsManager());
      return shPtr;
    }

    osgVector4 WindowsManager::getColor (const std::string& colorName)
    {
        if (colorName == "blue")
            return osgVector4 (0.f, 0.f, 1.f, 1.f);
        else if (colorName == "green")
            return osgVector4 (0.f, 1.f, 0.f, 1.f);
        else if (colorName == "red")
            return osgVector4 (1.f, 0.f, 0.f, 1.f);
        else if (colorName == "white")
            return osgVector4 (1.f, 1.f, 1.f, 1.f);
        else
            return osgVector4 (0.f, 0.f, 0.f, 1.f);
    }

    VisibilityMode WindowsManager::getVisibility (const std::string& vName)
    {
        if (vName == "OFF")
            return VISIBILITY_OFF;
        else if (vName == "ALWAYS_ON_TOP")
            return ALWAYS_ON_TOP;
        else if (vName == "ON")
            return VISIBILITY_ON;
        else {
            std::cout << "Visibility mode not known, visibility mode can be"
                " \"ON\",\"OFF\" or \"ALWAYS_ON_TOP\"." << std::endl;
            std::cout << "Visibility mode set to default = \"ON\"." << std::endl;
            return VISIBILITY_ON;
        }
    }

    WireFrameMode WindowsManager::getWire (const std::string& wireName)
    {
        if (wireName == "WIREFRAME")
            return WIREFRAME;
        else if (wireName == "FILL_AND_WIREFRAME")
            return FILL_AND_WIREFRAME;
        else if (wireName == "FILL")
            return FILL;
        else {
            std::cout << "Wire mode not known, wire mode can be \"FILL\","
                "\"WIREFRAME\" or \"FILL_AND_WIREFRAME\"." << std::endl;
            std::cout << "Wire mode set to default = \"FILL\"." << std::endl;
            return FILL;
        }
    }

    LightingMode WindowsManager::getLight (const std::string& lightName)
    {
        if (lightName == "OFF")
            return LIGHT_INFLUENCE_OFF;
        else if (lightName == "ON")
            return LIGHT_INFLUENCE_ON;
        else {
            std::cout << "Lighting mode not known, lighting mode can be "
                "\"ON\" or \"OFF\"." << std::endl;
            std::cout << "Lighting mode set to default = \"ON\"." << std::endl;
            return LIGHT_INFLUENCE_ON;
        }
    }

    std::string WindowsManager::parentName (const std::string& name)
    {
        std::string Name (name);
        std::string::iterator parentNameIt;
        for (std::string::iterator i = Name.end (); (*i) != char ('/') &&
                i != Name.begin (); i--) {
            parentNameIt = i;
        }
        parentNameIt--;

        std::string parentName;
        for (std::string::iterator i = Name.begin (); i != parentNameIt; i++) {
            parentName.push_back (*i);
        }

        return parentName;
    }

    NodePtr_t WindowsManager::find (const std::string name, GroupNodePtr_t)
    {
      NodeMapIt it = nodes_.find (name);
      if (it == nodes_.end ()) {
        std::string::size_type slash = name.find_first_of ('/');
        if (slash == std::string::npos)
          return NodePtr_t ();
        std::map<std::string, GroupNodePtr_t>::iterator itg
          = groupNodes_.find (name.substr (0, slash));
        if (itg == groupNodes_.end ())
          return NodePtr_t ();
        return find (name.substr (slash + 1), itg->second);
      }
      return it->second;
    }

    bool WindowsManager::nodeExists (const std::string& name)
    {
      NodeMapConstIt it = nodes_.find (name);
      return (it != nodes_.end ());
    }

    NodePtr_t WindowsManager::getNode (const std::string& name,
        bool throwIfDoesntExist) const
    {
      NodeMapConstIt it = nodes_.find (name);
      if (it == nodes_.end ()) {
        if (throwIfDoesntExist) {
          std::ostringstream oss;
          oss << "No node with name \"" << name << "\".";
          throw gepetto::Error (oss.str ().c_str ());
        } else
          return NodePtr_t();
      }
      return it->second;
    }

    template <typename NodeContainer_t> std::size_t WindowsManager::getNodes
      (const gepetto::corbaserver::Names_t& names, NodeContainer_t& nodes)
    {
      const std::size_t l = nodes.size();
      for (CORBA::ULong i = 0; i < names.length(); ++i) {
        std::string name (names[i]);
        NodePtr_t n = getNode (name);
        if (n) nodes.push_back (n);
        else std::cout << "Node \"" << name << "\" doesn't exist." << std::endl;
      }
      return nodes.size() - l;
    }

    template <typename Iterator, typename NodeContainer_t>
      std::size_t WindowsManager::getNodes
      (const Iterator& begin, const Iterator& end, NodeContainer_t& nodes)
    {
      const std::size_t l = nodes.size();
      for (Iterator it = begin; it != end; ++it) {
        std::string name (*it);
        NodePtr_t n = getNode (name);
        if (n) nodes.push_back (n);
        else std::cout << "Node \"" << name << "\" doesn't exist." << std::endl;
      }
      return nodes.size() - l;
    }

    void WindowsManager::initParent (const std::string& nodeName,
            NodePtr_t node)
    {
        GroupNodePtr_t groupNode = getGroup(parentName(nodeName));
        if (groupNode)
            groupNode->addChild (node);
    }

    void WindowsManager::addNode (const std::string& nodeName, NodePtr_t node)
    {
        nodes_[nodeName] = node;
    }

    void WindowsManager::addGroup (const std::string& groupName,
            GroupNodePtr_t group)
    {
        groupNodes_[groupName] = group;
        nodes_[groupName] = group;
    }

    void WindowsManager::threadRefreshing (WindowManagerPtr_t window)
    {
        while (!window->done ())
        {
            mtx_.lock ();
            window->frame ();
            mtx_.unlock ();
            boost::this_thread::sleep (boost::posix_time::milliseconds (rate_));
        }
    }

    UrdfFile::UrdfFile (const std::string& f)
      : filename (urdfParser::getFilename(f)) {
        struct stat buffer;
        if (stat (filename.c_str(), &buffer) != 0) {
          perror (filename.c_str());
          modTime = 0;
        }
        modTime = buffer.st_mtime;
      }

    bool WindowsManager::urdfUpToDate (const std::string nodeName,
        const std::string filename)
    {
      UrdfFileMap_t::const_iterator it = urdfFileMap_.find (nodeName);
      if (it == urdfFileMap_.end())
        return false;
      UrdfFile uf (filename);
      return it->second.modTime == uf.modTime;
    }

    void WindowsManager::registerUrdfNode (const std::string nodeName,
        const std::string filename)
    {
      urdfFileMap_[nodeName] = UrdfFile (filename);
    }

    //Public functions

    bool WindowsManager::setRate (const int& rate)
    {
        if (rate <= 0) {
            std::cout << "You should specify a positive rate" << std::endl;
            return false;
        }
        else {
            rate_ = rate;
            return true;
        }
    }

    WindowsManager::WindowID WindowsManager::createWindow (const std::string& wn)
    {
        WindowManagerPtr_t newWindow = WindowManager::create ();
        WindowID windowId = addWindow (wn, newWindow);
        boost::thread refreshThread (boost::bind
                (&WindowsManager::threadRefreshing,
                 this, newWindow));
        return windowId;
    }

    WindowsManager::WindowID WindowsManager::getWindowID (const std::string& wn)
    {
        WindowIDMap_t::iterator it = windowIDmap_.find (wn);
        if (it == windowIDmap_.end ())
            throw gepetto::Error ("There is no windows with that name");
        return it->second;
    }

    void WindowsManager::refresh ()
    {
        mtx_.lock ();
        //refresh scene with the new configuration
        std::for_each(newNodeConfigurations_.begin(),
            newNodeConfigurations_.end(),
            ApplyConfigurationFunctor());
        newNodeConfigurations_.clear ();
        mtx_.unlock ();
        if (autoCaptureTransform_) captureTransform ();
    }

    void WindowsManager::createScene (const std::string& sceneName)
    {
        if (nodeExists(sceneName)) {
            std::ostringstream oss;
            oss << "A scene with name, \"" << sceneName << "\" already exists.";
            throw gepetto::Error (oss.str ().c_str ());
        }
        else {
            GroupNodePtr_t mainNode = GroupNode::create (sceneName);
            mtx_.lock();
            addGroup (sceneName, mainNode);
            mtx_.unlock();
        }
    }

    void WindowsManager::createSceneWithFloor (const std::string& sceneName)
    {
        createScene(sceneName);
        addFloor((sceneName + "/floor").c_str());
    }

    bool WindowsManager::addSceneToWindow (const std::string& sceneName,
            WindowID windowId)
    {
        GroupNodePtr_t group = getGroup(sceneName, true);
        if (windowId < windowManagers_.size ()) {
            mtx_.lock();
            windowManagers_[windowId]->addNode (group);
            mtx_.unlock();
            return true;
        }
        else {
            std::cout << "Window ID " << windowId
                << " doesn't exist." << std::endl;
            return false;
        }
    }
  
     bool WindowsManager::attachCameraToNode(const std::string& nodeName, const WindowID windowId)
     {
        NodePtr_t node = getNode(nodeName, true);
        if (windowId > windowManagers_.size()) {
    	  std::cout << "Window ID" << windowId << "doesn't exist." << std::endl;
  	  return false;
        }
  	mtx_.lock();
	windowManagers_[windowId]->attachCameraToNode(node.get());
   	mtx_.unlock();
	return true;
     }

     bool WindowsManager::detachCamera(const WindowID windowId)
     {
        if (windowId > windowManagers_.size()) {
    	  std::cout << "Window ID " << windowId << " doesn't exist."
  		    << std::endl;
  	  return false;
        }
  	mtx_.lock();
	windowManagers_[windowId]->detachCamera();
   	mtx_.unlock();       
	return true;
     }

    bool WindowsManager::addFloor(const std::string& floorName)
    {
        RETURN_IF_NODE_EXISTS(floorName);
        LeafNodeGroundPtr_t floor = LeafNodeGround::create (floorName);
        mtx_.lock();
        WindowsManager::initParent (floorName, floor);
        addNode (floorName, floor);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addBox (const std::string& boxName,
            const float& boxSize1,
            const float& boxSize2,
            const float& boxSize3,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(boxName);

        LeafNodeBoxPtr_t box = LeafNodeBox::create
          (boxName, osgVector3 (boxSize1, boxSize2, boxSize3), color);
        mtx_.lock();
        WindowsManager::initParent (boxName, box);
        addNode (boxName, box);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addCapsule (const std::string& capsuleName,
            const float radius,
            const float height,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(capsuleName);

        LeafNodeCapsulePtr_t capsule = LeafNodeCapsule::create (capsuleName, radius, height, color);
        mtx_.lock();
        WindowsManager::initParent (capsuleName, capsule);
        addNode (capsuleName, capsule);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addArrow (const std::string& arrowName,
            const float radius,
            const float length,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(arrowName);

        LeafNodeArrowPtr_t arrow = LeafNodeArrow::create (arrowName, color, radius, length);
        mtx_.lock();
        WindowsManager::initParent (arrowName, arrow);
        addNode (arrowName, arrow);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addRod (const std::string& rodName,
            const Color_t& color,
            const float radius,
            const float length,
            short maxCapsule
            ){

      RETURN_IF_NODE_EXISTS(rodName);

      NodeRodPtr_t rod = NodeRod::create(rodName,color,radius,length,maxCapsule);
      mtx_.lock();
      WindowsManager::initParent (rodName, rod);
      addNode (rodName, rod);
      for(size_t i = 0 ; i < (size_t) maxCapsule ; i++)
        addNode(rod->getCapsuleName(i),rod->getCapsule(i));
      mtx_.unlock();
      return true;
    }

    bool WindowsManager::resizeCapsule(const std::string& capsuleName, float newHeight) throw(std::exception){
        NodePtr_t node = getNode(capsuleName, true);
        try{
          LeafNodeCapsulePtr_t cap = boost::dynamic_pointer_cast<LeafNodeCapsule>(node);
          cap->resize(newHeight);
        }catch (const std::exception& exc) {
          std::cout <<capsuleName << "isn't a capsule."  << std::endl;
          return false;
        }
        return true;
    }

    bool WindowsManager::resizeArrow(const std::string& arrowName ,float newRadius, float newLength) throw(std::exception){
        NodePtr_t node = getNode(arrowName, true);
        try{
          LeafNodeArrowPtr_t arrow = boost::dynamic_pointer_cast<LeafNodeArrow>(node);
          arrow->resize(newRadius,newLength);
        }catch (const std::exception& exc) {
          std::cout <<arrowName << "isn't an arrow."  << std::endl;
          return false;
        }

        return true;
    }

    bool WindowsManager::addMesh (const std::string& meshName,
            const std::string& meshPath)
    {
        RETURN_IF_NODE_EXISTS(meshName);
        try {
          LeafNodeColladaPtr_t mesh = LeafNodeCollada::create
            (meshName, meshPath);
          mtx_.lock();
          WindowsManager::initParent (meshName, mesh);
          addNode (meshName, mesh);
          mtx_.unlock();
          return true;
        } catch (const std::exception& exc) {
          std::cout << exc.what() << std::endl;
          mtx_.unlock();
          return false;
        }
    }

    bool WindowsManager::addCone (const std::string& coneName,
            const float radius, const float height,
            const Color_t&)
    {
        RETURN_IF_NODE_EXISTS(coneName);

        LeafNodeConePtr_t cone = LeafNodeCone::create
          (coneName, radius, height);
        mtx_.lock();
        WindowsManager::initParent (coneName, cone);
        addNode (coneName, cone);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addCylinder (const std::string& cylinderName,
            const float radius,
            const float height,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(cylinderName);

        LeafNodeCylinderPtr_t cylinder = LeafNodeCylinder::create
          (cylinderName, radius, height, color);
        mtx_.lock();
        WindowsManager::initParent (cylinderName, cylinder);
        addNode (cylinderName, cylinder);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addSphere (const std::string& sphereName,
            const float radius,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(sphereName);

        LeafNodeSpherePtr_t sphere = LeafNodeSphere::create
          (sphereName, radius, color);
        mtx_.lock();
        WindowsManager::initParent (sphereName, sphere);
        addNode (sphereName, sphere);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addLight (const std::string& lightName,
            const WindowID wid,
            const float radius,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(lightName);

        LeafNodeLightPtr_t light = LeafNodeLight::create
          (lightName, radius, color);
        mtx_.lock();
        WindowsManager::initParent (lightName, light);
        addNode (lightName, light);
        light->setRoot (windowManagers_[wid]->getScene ());
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addLine (const std::string& lineName,
            const osgVector3& pos1,
            const osgVector3& pos2,
            const Color_t& color)
    {
        RETURN_IF_NODE_EXISTS(lineName);

        LeafNodeLinePtr_t line = LeafNodeLine::create (lineName, pos1, pos2, color);
        mtx_.lock();
        WindowsManager::initParent (lineName, line);
        addNode (lineName, line);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::addCurve (const std::string& curveName,
            const PositionSeq& pos,
            const Color_t& color)
    {
        if (nodes_.find (curveName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << curveName
                << "\" already exist." << std::endl;
            return false;
        }
        else {
            if (pos.length () < 2) {
              std::cout << "Need at least two points" << std::endl;
              return false;
            }
            ::osg::Vec3ArrayRefPtr values = new ::osg::Vec3Array;
            std::size_t i = 0;
            for (i = 0; i < pos.length (); ++i) {
	      using CORBA::ULong;
              values->push_back (::osg::Vec3 (pos[(ULong)i][0],pos[(ULong)i][1],
					      pos[(ULong)i][2]));
            }
            LeafNodeLinePtr_t curve = LeafNodeLine::create (curveName, values, color);
            curve->setMode (GL_LINE_STRIP);
            mtx_.lock();
            WindowsManager::initParent (curveName, curve);
            addNode (curveName, curve);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::setCurveMode (const std::string& curveName, const GLenum mode)
    {
        NodePtr_t node = find (curveName);
        if (!node) {
            std::cerr << "Node \"" << curveName << "\" not found." << std::endl;
            return false;
        } else {
            LeafNodeLinePtr_t curve (boost::dynamic_pointer_cast
                <LeafNodeLine> (node));
            if (!curve) {
              std::cerr << "Node \"" << curveName << "\" is not a curve." << std::endl;
              return false;
            }
            mtx_.lock();
            curve->setMode (mode);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::addTriangleFace (const std::string& faceName,
            const osgVector3& pos1,
            const osgVector3& pos2,
            const osgVector3& pos3,
            const Color_t& color)
    {
        if (nodes_.find (faceName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << faceName
                << "\" already exist." << std::endl;
            return false;
        }
        else {
            LeafNodeFacePtr_t face = LeafNodeFace::create (faceName, pos1, pos2, pos3, color);
            mtx_.lock();
            WindowsManager::initParent (faceName, face);
            addNode (faceName, face);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::addSquareFace (const std::string& faceName,
            const osgVector3& pos1,
            const osgVector3& pos2,
            const osgVector3& pos3,
            const osgVector3& pos4,
            const Color_t& color)
    {
        if (nodes_.find (faceName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << faceName
                << "\" already exist." << std::endl;
            return false;
        }
        else {
            LeafNodeFacePtr_t face = LeafNodeFace::create
                (faceName, pos1, pos2, pos3, pos4, color);
            mtx_.lock();
            WindowsManager::initParent (faceName, face);
            addNode (faceName, face);
	    mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::addXYZaxis (const std::string& nodeName,const Color_t& color, float radius, float sizeAxis)
    {

          if (nodes_.find (nodeName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << nodeName
                  << "\" already exist." << std::endl;
            return false;
          }
          else {
            LeafNodeXYZAxisPtr_t axis = LeafNodeXYZAxis::create
              (nodeName,color,radius,sizeAxis);
            mtx_.lock();
            WindowsManager::initParent (nodeName, axis);
            addNode (nodeName, axis);
            mtx_.unlock();
            return true;
          }
    }

    bool WindowsManager::createRoadmap(const std::string& roadmapName,const Color_t& colorNode, float radius, float sizeAxis, const Color_t& colorEdge){
        if (nodes_.find (roadmapName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << roadmapName
                << "\" already exist." << std::endl;
            return false;
        }
        else {
            RoadmapViewerPtr_t rm = RoadmapViewer::create(roadmapName,colorNode,radius,sizeAxis,colorEdge);
            mtx_.lock();
            WindowsManager::initParent (roadmapName, rm);
            addNode (roadmapName, rm);
            mtx_.unlock();
            roadmapNodes_[roadmapName]=rm;
            return true;
        }
    }

    bool WindowsManager::addEdgeToRoadmap(const std::string& nameRoadmap, const osgVector3& posFrom, const osgVector3& posTo){
        if (roadmapNodes_.find (nameRoadmap) == roadmapNodes_.end ()) {
            //no node named nodeName
            std::cout << "No roadmap named \"" << nameRoadmap << "\"" << std::endl;
            return false;
        }
        else {
            RoadmapViewerPtr_t rm_ptr = roadmapNodes_[nameRoadmap];
          //  mtx_.lock(); mtx is now locked only when required in addEdge
            rm_ptr->addEdge(posFrom,posTo,mtx_);
         //   mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::addNodeToRoadmap(const std::string& nameRoadmap, const Configuration& conf)
    {
        if (roadmapNodes_.find (nameRoadmap) == roadmapNodes_.end ()) {
            //no node named nodeName
            std::cout << "No roadmap named \"" << nameRoadmap << "\"" << std::endl;
            return false;
        }
        else {
            RoadmapViewerPtr_t rm_ptr = roadmapNodes_[nameRoadmap];
           // mtx_.lock();
            rm_ptr->addNode(conf.position,conf.quat,mtx_);
           // mtx_.unlock();
            return true;
        }
    }

    std::vector<std::string> WindowsManager::getNodeList ()
    {
        std::vector<std::string> l;
        for (NodeMapIt it=nodes_.begin (); it!=nodes_.end (); ++it) {
            l.push_back (it->first);
        }
        return l;
    }
		
    std::vector<std::string> WindowsManager::getGroupNodeList (const std::string& group)
    {
        std::vector<std::string> l;
        std::map<std::string, GroupNodePtr_t>::iterator it= groupNodes_.find(group);
        if(it == groupNodes_.end())
        {
            std::cout << "Unexisting group: " << group << std::endl;
        }
        else
        {
            std::cout << "List of Nodes in group :" << group << std::endl;
            GroupNodePtr_t group = it->second;
            for(std::size_t i = 0; i < group->getNumOfChildren(); ++i)
            {
                NodePtr_t node = group->getChild(i);
                l.push_back(node->getID());
                std::cout << "   " << node->getID() << std::endl;
            }
        }
        return l;
    }
	
    std::vector<std::string> WindowsManager::getSceneList ()
    {
        std::vector<std::string> l;
        std::cout << "List of GroupNodes :" << std::endl;
        for (std::map<std::string, GroupNodePtr_t>::iterator it=
                groupNodes_.begin (); it!=groupNodes_.end (); ++it) {
            std::cout << "   " << it->first << std::endl;
            l.push_back (it->first);
        }
        return l;
    }

    std::vector<std::string> WindowsManager::getWindowList ()
    {
        std::vector<std::string> l;
        for (WindowIDMap_t::const_iterator it = windowIDmap_.begin ();
                it!=windowIDmap_.end (); ++it) {
            l.push_back (it->first);
        }
        return l;
    }

    bool WindowsManager::createGroup (const std::string& groupName)
    {
        if (nodes_.find (groupName) != nodes_.end ()) {
            std::cout << "You need to chose an other name, \"" << groupName
                << "\" already exist." << std::endl;
            return false;
        }
        else {
            GroupNodePtr_t groupNode = GroupNode::create (groupName);
            mtx_.lock();
            WindowsManager::initParent (groupName, groupNode);
            addGroup (groupName, groupNode);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::urdfNodeMustBeAdded (const std::string& nodeName,
                const std::string& filename)
    {
      if (nodes_.find (nodeName) != nodes_.end ()) {
        if (urdfUpToDate (nodeName, filename)) {
          std::cout << "Urdf already loaded: " << nodeName << std::endl;
          return false;
        } else {
          // Erase existing node.
          std::cout << "Urdf deleted: " << nodeName << std::endl;
          deleteNode (nodeName.c_str(), false);
        }
      }
      return true;
    }

    bool WindowsManager::addURDF (const std::string& urdfName,
            const std::string& urdfPath,
            const std::string& /*urdfPackagePathCorba*/)
    {
        if (urdfNodeMustBeAdded (urdfName, urdfPath)) {
          GroupNodePtr_t urdf = urdfParser::parse (urdfName, urdfPath);
          NodePtr_t link;
          for (std::size_t i=0; i< urdf->getNumOfChildren (); i++) {
            link = urdf->getChild (i);
            nodes_[link->getID ()] = link;
            GroupNodePtr_t groupNode (boost::dynamic_pointer_cast
                <GroupNode> (link));
            if (groupNode) {
              for (std::size_t j=0; j < groupNode->getNumOfChildren ();
                  ++j) {
                NodePtr_t object (groupNode->getChild (j));
                nodes_ [object->getID ()] = object;
              }
            }
          }
          mtx_.lock();
          WindowsManager::initParent (urdfName, urdf);
          addGroup (urdfName, urdf);
          registerUrdfNode (urdfName, urdfPath);
          mtx_.unlock();
          return true;
        }
        return false;
    }

    bool WindowsManager::addUrdfCollision (const std::string& urdfName,
            const std::string& urdfPath, const std::string& /*urdfPackagePathCorba*/)
    {
        if (urdfNodeMustBeAdded (urdfName, urdfPath)) {
            GroupNodePtr_t urdf = urdfParser::parse
                (urdfName, urdfPath, "collision");
            NodePtr_t link;
            for (std::size_t i=0; i< urdf->getNumOfChildren (); i++) {
                link = urdf->getChild (i);
                nodes_[link->getID ()] = link;
		GroupNodePtr_t groupNode (boost::dynamic_pointer_cast
					  <GroupNode> (link));
		if (groupNode) {
		  for (std::size_t j=0; j < groupNode->getNumOfChildren ();
		       ++j) {
		    NodePtr_t object (groupNode->getChild (j));
		    nodes_ [object->getID ()] = object;
		  }
		}
            }
            mtx_.lock();
            WindowsManager::initParent (urdfName, urdf);
            addGroup (urdfName, urdf);
            registerUrdfNode (urdfName, urdfPath);
            mtx_.unlock();
            return true;
        }
        return false;
    }

    void WindowsManager::addUrdfObjects (const std::string& urdfName,
            const std::string& urdfPath,
            const std::string& /*urdfPackagePathCorba*/,
            bool visual)
    {
        if (urdfName == "") {
            throw gepetto::Error ("Parameter nodeName cannot be empty in "
                    "idl request addUrdfObjects.");
        }
        if (urdfNodeMustBeAdded (urdfName, urdfPath)) {
          GroupNodePtr_t urdf = urdfParser::parse
            (urdfName, urdfPath, visual ? "visual" : "collision", "object");
          NodePtr_t link;
          for (std::size_t i=0; i< urdf->getNumOfChildren (); i++) {
            link = urdf->getChild (i);
            nodes_[link->getID ()] = link;
            GroupNodePtr_t groupNode (boost::dynamic_pointer_cast
                <GroupNode> (link));
            if (groupNode) {
              for (std::size_t j=0; j < groupNode->getNumOfChildren ();
                  ++j) {
                NodePtr_t object (groupNode->getChild (j));
                nodes_ [object->getID ()] = object;
              }
            }
          }
          mtx_.lock();
          WindowsManager::initParent (urdfName, urdf);
          addGroup (urdfName, urdf);
          registerUrdfNode (urdfName, urdfPath);
          mtx_.unlock();
        }
    }

    bool WindowsManager::addToGroup (const std::string& nodeName,
            const std::string& groupName)
    {
        if (nodes_.find (nodeName) == nodes_.end () ||
                groupNodes_.find (groupName) == groupNodes_.end ()) {
            std::cout << "Node name \"" << nodeName << "\" and/or groupNode \""
                << groupName << "\" doesn't exist." << std::endl;
            return false;
        }
        else {
            mtx_.lock();// if addChild is called in the same time as osg::frame(), gepetto-viewer crash
            groupNodes_[groupName]->addChild (nodes_[nodeName]);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::removeFromGroup (const std::string& nodeName,
            const std::string& groupName)
    {
        if (nodes_.find (nodeName) == nodes_.end () ||
                groupNodes_.find (groupName) == groupNodes_.end ()) {
            std::cout << "Node name \"" << nodeName << "\" and/or groupNode \""
                << groupName << "\" doesn't exist." << std::endl;
            return false;
        }
        else {
            mtx_.lock();
            groupNodes_[groupName]->removeChild(nodes_[nodeName]);
	    mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::deleteNode (const std::string& nodeName, bool all)
    {
        NodePtr_t n = getNode (nodeName);
        if (!n) return false;
        else {
            /// Check if it is a group
            std::map<std::string, GroupNodePtr_t>::iterator it =
              groupNodes_.find(nodeName);
            if (it != groupNodes_.end ()) {
              if (all) {
                std::vector <std::string> names(it->second->getNumOfChildren ());
                for (std::size_t i = 0; i < names.size(); ++i)
                  names[i] = it->second->getChild (i)->getID();
                it->second->removeAllChildren ();
                for (std::size_t i = 0; i < names.size(); ++i)
                  deleteNode (names[i].c_str(), all);
              }
              groupNodes_.erase (nodeName);
            }
            std::map<std::string, GroupNodePtr_t>::iterator itg;
            for (itg = groupNodes_.begin (); itg != groupNodes_.end(); ++itg) {
              if (itg->second && itg->second->hasChild (n))
                itg->second->removeChild(n);
            }
            nodes_.erase (nodeName);
            return true;
        }
    }

    bool WindowsManager::applyConfiguration (const std::string& nodeName,
            const Configuration& configuration)
    {
        NodePtr_t updatedNode = find (nodeName);
        if (!updatedNode) {
            //no node named nodeName
            std::cout << "No Node named \"" << nodeName << "\"" << std::endl;
            return false;
        }
        else {
            NodeConfiguration newNodeConfiguration;
            newNodeConfiguration.node = updatedNode;
            newNodeConfiguration.position = configuration.position;
            newNodeConfiguration.quat = configuration.quat;
            mtx_.lock();
            newNodeConfigurations_.push_back (newNodeConfiguration);
            mtx_.unlock();
            return true;
        }
    }

    bool WindowsManager::addLandmark (const std::string& nodeName,
            float size)
    {
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->addLandmark (size);
	mtx_.unlock();
        return true;
    }

    bool WindowsManager::deleteLandmark (const std::string& nodeName)
    {
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->deleteLandmark ();
	mtx_.unlock();
        return true;
    }

    Configuration WindowsManager::getStaticTransform (const std::string& nodeName) const
    {
        NodePtr_t node = getNode(nodeName, true);
        return Configuration(node->getStaticPosition (),
                             node->getStaticRotation ());
    }
  
  bool WindowsManager::setStaticTransform (const std::string& nodeName,
      const Configuration& transform)
  {
    NodeMapConstIt it = nodes_.find(nodeName);
    if (it == nodes_.end ()) {
      std::cout << "Node \"" << nodeName << "\" doesn't exist." << std::endl;
      return false;
    }
    
    mtx_.lock();
    it->second->setStaticTransform(transform.position,transform.quat);
    mtx_.unlock();
    return true;
  }

    bool WindowsManager::setVisibility (const std::string& nodeName,
            const std::string& visibilityMode)
    {
        VisibilityMode visibility =  getVisibility (visibilityMode);
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->setVisibilityMode (visibility);
	mtx_.unlock();
        return true;
    }

    bool WindowsManager::setScale(const std::string& nodeName, const osgVector3& scale)
    {
        osg::Vec3d vecScale(scale[0],scale[1],scale[2]);
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }

        mtx_.lock();
        nodes_[nodeName]->setScale(vecScale);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::setScale(const std::string& nodeName, const float& scale)
    {
      return setScale(nodeName, osgVector3(scale, scale, scale));
    }

    bool WindowsManager::setScale(const std::string& nodeName, const int& scalePercentage)
    {
      return setScale (nodeName, (value_type)scalePercentage / 100);
    }

    bool WindowsManager::setAlpha(const std::string& nodeName, const float& alpha)
    {
        if (nodes_.find (nodeName) == nodes_.end ()) {
    	  std::cout << "Node \"" << nodeName << "\" doesn't exist."
  		    << std::endl;
  	  return false;
        }
  	mtx_.lock();
        nodes_[nodeName]->setAlpha (alpha);
   	mtx_.unlock();
        return true;
    }

    bool WindowsManager::setAlpha(const std::string& nodeName, const int& alphaPercentage)
    {
      return setAlpha (nodeName, (float)alphaPercentage / 100);
    }

    bool WindowsManager::setColor(const std::string& nodeName, const Color_t& color)
    {
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
        osgVector4 vecColor(color[0],color[1],color[2],color[3]);
        mtx_.lock();
        nodes_[nodeName]->setColor (vecColor);
        mtx_.unlock();
        return true;
    }

    bool WindowsManager::setWireFrameMode (const std::string& nodeName,
            const std::string& wireFrameMode)
    {
        WireFrameMode wire = getWire (wireFrameMode);
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->setWireFrameMode (wire);
	mtx_.unlock();
	return true;
    }

    bool WindowsManager::setLightingMode (const std::string& nodeName,
            const std::string& lightingMode)
    {
        LightingMode light = getLight (lightingMode);
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->setLightingMode (light);
	mtx_.unlock();
        return true;
    }

    bool WindowsManager::setHighlight (const std::string& nodeName,
            int state)
    {
        if (nodes_.find (nodeName) == nodes_.end ()) {
            std::cout << "Node \"" << nodeName << "\" doesn't exist."
                << std::endl;
            return false;
        }
	mtx_.lock();
        nodes_[nodeName]->setHighlightState (state);
	mtx_.unlock();
        return true;
    }

    bool WindowsManager::startCapture (const WindowID windowId, const std::string& filename,
            const std::string& extension)
    {
        if (windowId < windowManagers_.size ()) {
            mtx_.lock();
            windowManagers_[windowId]->startCapture
                (std::string (filename), std::string (extension));
            mtx_.unlock();
            return true;
        }
        else {
            std::cout << "Window ID " << windowId
                << " doesn't exist." << std::endl;
            return false;
        }
    }

    bool WindowsManager::stopCapture (const WindowID windowId)
    {
        if (windowId < windowManagers_.size ()) {
            mtx_.lock();
            windowManagers_[windowId]->stopCapture ();
            mtx_.unlock();
            return true;
        }
        else {
            std::cout << "Window ID " << windowId
                << " doesn't exist." << std::endl;
            return false;
        }
    }

    bool WindowsManager::setCaptureTransform (const std::string& filename,
        const std::vector<std::string>& nodeNames)
    {
        blenderCapture_.nodes_.clear ();
        std::size_t nb = getNodes (nodeNames.begin(), nodeNames.end(),
            blenderCapture_.nodes_);
        blenderCapture_.writer_visitor_->writer_ =
          new YamlTransformWriter (filename);
        return nb == nodeNames.size();
    }

    void WindowsManager::captureTransformOnRefresh (bool autoCapture)
    {
      autoCaptureTransform_ = autoCapture;
    }

    void WindowsManager::captureTransform ()
    {
        mtx_.lock ();
        blenderCapture_.captureFrame ();
        mtx_.unlock ();
    }

    bool WindowsManager::writeBlenderScript (const std::string& filename,
        const std::vector<std::string>& nodeNames)
    {
      std::vector<NodePtr_t> nodes;
      std::size_t nb = getNodes (nodeNames.begin(), nodeNames.end(), nodes);
      if (nb != nodeNames.size())
        throw std::invalid_argument ("Could not find one of the nodes");
      BlenderGeomWriterVisitor visitor (filename);
      for (std::size_t i = 0; i < nodes.size(); ++i)
        nodes[i]->accept(visitor);
      return true;
    }

    bool WindowsManager::writeNodeFile (const std::string& nodename,
        const std::string& filename)
    {
        const std::string name (nodename);
        NodeMapIt it = nodes_.find (name);
        if (it == nodes_.end ()) {
            std::cout << "Node \"" << nodename << "\" doesn't exist."
                << std::endl;
            return false;
        }
        mtx_.lock();
        osg::ref_ptr <osgDB::Options> os = new osgDB::Options;
        os->setOptionString ("NoExtras");
        bool ret = osgDB::writeNodeFile (*it->second->asGroup (),
            std::string (filename), os.get());
        mtx_.unlock();
        return ret;
    }

    bool WindowsManager::writeWindowFile (const WindowID windowId,
        const std::string& filename)
    {
        if (windowId < windowManagers_.size ()) {
            mtx_.lock();
            bool ret = windowManagers_[windowId]->writeNodeFile (std::string (filename));
            mtx_.unlock();
            return ret;
        }
        else {
            std::cout << "Window ID " << windowId
                << " doesn't exist." << std::endl;
            return false;
        }
    }

    WindowManagerPtr_t WindowsManager::getWindowManager (const WindowID wid)
    {
      if (wid < windowManagers_.size ()) {
        return windowManagers_[wid];
      }
      else {
        std::cout << "Window ID " << wid << " doesn't exist." << std::endl;
        return WindowManagerPtr_t ();
      }
    }

    GroupNodePtr_t WindowsManager::getGroup (const std::string groupName,
        bool throwIfDoesntExist) const
    {
        std::map<std::string, GroupNodePtr_t>::const_iterator it =
            groupNodes_.find (groupName);
        if (it == groupNodes_.end ()) {
          if (throwIfDoesntExist) {
            std::ostringstream oss;
            oss << "No group with name \"" << groupName << "\".";
            throw gepetto::Error (oss.str ().c_str ());
          } else
            return GroupNodePtr_t ();
        }
        return it->second;
    }

    Configuration WindowsManager::getNodeGlobalTransform(const std::string nodeName)
    {
        NodePtr_t node = getNode(nodeName, true);
        std::pair<osgVector3, osgQuat> posQuat = node->getGlobalTransform();
        return Configuration(posQuat.first, posQuat.second);
    }
    
    bool WindowsManager::setBackgroundColor1(const WindowID windowId,const Color_t& color)
    {
      mtx_.lock();
      windowManagers_[windowId]->setBackgroundColor1(color);
      mtx_.unlock();
      return true;
    }
  
  bool WindowsManager::setBackgroundColor2(const WindowID windowId,const Color_t& color)
  {
    mtx_.lock();
    windowManagers_[windowId]->setBackgroundColor2(color);
    mtx_.unlock();
    return true;
  }
  
} // namespace graphics
