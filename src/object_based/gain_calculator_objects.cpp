#include "ear/object_based/gain_calculator_objects.hpp"

#include <cmath>
#include <functional>
#include "ear/common/geom.hpp"
#include "ear/common/helpers/eigen_helpers.hpp"
#include "ear/exceptions.hpp"

namespace ear {

  // throws an exception if the given component is not implemented
  struct throw_if_not_implemented : public boost::static_visitor<void> {
    void operator()(const CartesianPosition&) const {
      throw not_implemented("cartesian");
    }
    void operator()(const PolarObjectDivergence& divergence) const {
      if (divergence.divergence != 0.0) throw not_implemented("divergence");
    }
    void operator()(const CartesianObjectDivergence& divergence) const {
      if (divergence.divergence != 0.0) throw not_implemented("divergence");
    }
    template <typename T>
    void operator()(const T&) const {}
  };

  GainCalculatorObjectsImpl::GainCalculatorObjectsImpl(const Layout& layout)
      : _layout(layout),
        _pointSourcePanner(configurePolarPanner(_layout.withoutLfe())),
        _screenEdgeLockHandler(ScreenEdgeLockHandler(_layout.screen())),
        _screenScaleHandler(ScreenScaleHandler(_layout.screen())),
        _channelLockHandler(ChannelLockHandler(_layout.withoutLfe())),
        _polarExtentPanner(PolarExtentPanner(_pointSourcePanner)),
        _zoneExclusionHandler(ZoneExclusionHandler(_layout.withoutLfe())),
        _isLfe(copy_vector<decltype(_isLfe)>(layout.isLfe())){};

  void GainCalculatorObjectsImpl::calculate(const ObjectsTypeMetadata& metadata,
                                            OutputGains& direct,
                                            OutputGains& diffuse,
                                            const WarningCB& warning_cb) {
    if (metadata.cartesian) throw not_implemented("cartesian");
    boost::apply_visitor(throw_if_not_implemented(), metadata.position);
    boost::apply_visitor(throw_if_not_implemented(), metadata.objectDivergence);
    if (metadata.channelLock.flag) throw not_implemented("channelLock");
    if (metadata.zoneExclusion.zones.size())
      throw not_implemented("zoneExclusion");
    if (metadata.screenRef) throw not_implemented("screenRef");

    Eigen::Vector3d position =
        toCartesianVector3d(boost::get<PolarPosition>(metadata.position));
    auto pv = _polarExtentPanner.handle(position, metadata.width,
                                        metadata.height, metadata.depth);
    pv *= metadata.gain;

    Eigen::VectorXd pv_full = Eigen::VectorXd::Zero(_isLfe.size());
    mask_write(pv_full, !_isLfe, pv);

    // apply diffuse split
    direct.write_vector(pv_full * std::sqrt(1.0 - metadata.diffuse));
    diffuse.write_vector(pv_full * std::sqrt(metadata.diffuse));
  }

}  // namespace ear
